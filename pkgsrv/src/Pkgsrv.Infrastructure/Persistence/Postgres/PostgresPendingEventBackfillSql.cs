namespace Pkgsrv.Infrastructure.Persistence.Postgres;

internal static class PostgresPendingEventBackfillSql
{
    public const string Sql =
        """
        DO $pkgsrv$
        DECLARE
            package_row record;
            descriptor_row record;
            pending_events_json text;
            descriptor_set jsonb;
            event_type text;
            occurred_at_text text;
            fractional_text text;
            whole_seconds bigint;
            occurred_at_ticks bigint;
            idempotency_key text;
            payload text;
        BEGIN
            LOCK TABLE packages IN ACCESS EXCLUSIVE MODE;

            DROP TABLE IF EXISTS pg_temp.pkgsrv_processed_packages;
            CREATE TEMP TABLE pkgsrv_processed_packages (
                "Name" text NOT NULL,
                "Version" text NOT NULL,
                PRIMARY KEY ("Name", "Version")
            ) ON COMMIT DROP;

            FOR package_row IN
                SELECT
                    "Name",
                    "Version",
                    "SourceUrl",
                    "SourceCommit",
                    "ToolchainName",
                    "ToolchainVersion",
                    "ContentHash",
                    "Provenance",
                    "SignatureStatus",
                    "SignatureAlgorithm",
                    "SignatureValue",
                    "BuildIsolation",
                    "AllowNetwork",
                    "TimeoutTicks",
                    "State",
                    "BuildRequestedAt",
                    "PendingEventsJson"
                FROM packages
                WHERE "PendingEventsJson" IS NOT NULL
                  AND btrim("PendingEventsJson") NOT IN ('', '[]')
                ORDER BY "Name", "Version"
            LOOP
                pending_events_json := package_row."PendingEventsJson";

                BEGIN
                    descriptor_set := pending_events_json::jsonb;
                EXCEPTION WHEN invalid_text_representation THEN
                    RAISE EXCEPTION
                        'Package % % has malformed PendingEventsJson',
                        package_row."Name",
                        package_row."Version";
                END;

                IF jsonb_typeof(descriptor_set) <> 'array' THEN
                    RAISE EXCEPTION
                        'Package % % PendingEventsJson is not a JSON array',
                        package_row."Name",
                        package_row."Version";
                END IF;

                IF package_row."SignatureStatus" NOT IN (0, 1, 2) THEN
                    RAISE EXCEPTION
                        'Package % % has unsupported signature status %',
                        package_row."Name",
                        package_row."Version",
                        package_row."SignatureStatus";
                END IF;

                IF package_row."SignatureStatus" = 2
                   AND (
                       package_row."SignatureAlgorithm" IS NULL
                       OR btrim(package_row."SignatureAlgorithm") = ''
                       OR package_row."SignatureValue" IS NULL
                       OR btrim(package_row."SignatureValue") = ''
                   ) THEN
                    RAISE EXCEPTION
                        'Package % % has a signed status without signature fields',
                        package_row."Name",
                        package_row."Version";
                END IF;

                IF package_row."BuildIsolation" NOT IN (0, 1, 2) THEN
                    RAISE EXCEPTION
                        'Package % % has unsupported build isolation %',
                        package_row."Name",
                        package_row."Version",
                        package_row."BuildIsolation";
                END IF;

                IF package_row."TimeoutTicks"
                   NOT BETWEEN 10000000 AND 36000000000 THEN
                    RAISE EXCEPTION
                        'Package % % has an unsupported build timeout % ticks',
                        package_row."Name",
                        package_row."Version",
                        package_row."TimeoutTicks";
                END IF;

                IF package_row."State" NOT IN (0, 1, 2) THEN
                    RAISE EXCEPTION
                        'Package % % has unsupported state %',
                        package_row."Name",
                        package_row."Version",
                        package_row."State";
                END IF;

                FOR descriptor_row IN
                    SELECT descriptor, ordinality
                    FROM jsonb_array_elements(
                        descriptor_set
                    ) WITH ORDINALITY AS events(descriptor, ordinality)
                LOOP
                    event_type := descriptor_row.descriptor->>'type';

                    IF event_type IS NULL
                       OR event_type NOT IN (
                           'PackageRegistered',
                           'PackageBuildRequested'
                       ) THEN
                        RAISE EXCEPTION
                            'Unsupported pending package event type % for package % % at descriptor %',
                            coalesce(event_type, '<null>'),
                            package_row."Name",
                            package_row."Version",
                            descriptor_row.ordinality;
                    END IF;

                    IF event_type = 'PackageBuildRequested'
                       AND package_row."BuildRequestedAt" IS NULL THEN
                        RAISE EXCEPTION
                            'Package % % has a build event descriptor without a persisted build request timestamp',
                            package_row."Name",
                            package_row."Version";
                    END IF;

                    IF descriptor_row.descriptor->>'name'
                       IS DISTINCT FROM package_row."Name" THEN
                        RAISE EXCEPTION
                            'Pending package event name % does not match package row % for package % %',
                            coalesce(descriptor_row.descriptor->>'name', '<null>'),
                            package_row."Name",
                            package_row."Name",
                            package_row."Version";
                    END IF;

                    IF descriptor_row.descriptor->>'version'
                       IS DISTINCT FROM package_row."Version" THEN
                        RAISE EXCEPTION
                            'Pending package event version % does not match package row % for package % %',
                            coalesce(descriptor_row.descriptor->>'version', '<null>'),
                            package_row."Version",
                            package_row."Name",
                            package_row."Version";
                    END IF;

                    occurred_at_text := descriptor_row.descriptor->>'occurredAt';

                    IF occurred_at_text IS NULL
                       OR btrim(occurred_at_text) = '' THEN
                        RAISE EXCEPTION
                            'Pending package event for package % % at descriptor % has no occurrence timestamp',
                            package_row."Name",
                            package_row."Version",
                            descriptor_row.ordinality;
                    END IF;

                    IF occurred_at_text !~
                       '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]{1,7})?(Z|[+-][0-9]{2}:[0-9]{2})$' THEN
                        RAISE EXCEPTION
                            'Pending package event timestamp % for package % % is not an ISO-8601 timestamp with at most 100ns precision',
                            occurred_at_text,
                            package_row."Name",
                            package_row."Version";
                    END IF;

                    fractional_text := substring(
                        occurred_at_text from '\.([0-9]+)');

                    whole_seconds := floor(extract(epoch from (
                        regexp_replace(occurred_at_text, '\.[0-9]+', '')
                    )::timestamptz))::bigint;

                    occurred_at_ticks :=
                        (whole_seconds + 62135596800) * 10000000
                        + coalesce(
                            rpad(fractional_text, 7, '0')::bigint,
                            0);

                    IF occurred_at_ticks < 0
                       OR occurred_at_ticks > 3155378975999999999 THEN
                        RAISE EXCEPTION
                            'Pending package event timestamp % for package % % is outside the .NET DateTimeOffset range',
                            occurred_at_text,
                            package_row."Name",
                            package_row."Version";
                    END IF;

                    IF event_type = 'PackageRegistered' THEN
                        payload :=
                            '{"name":' || to_json(package_row."Name")::text
                            || ',"version":'
                            || to_json(package_row."Version")::text
                            || ',"sourceUrl":'
                            || to_json(package_row."SourceUrl")::text
                            || ',"sourceCommit":'
                            || to_json(package_row."SourceCommit")::text
                            || ',"toolchainName":'
                            || to_json(package_row."ToolchainName")::text
                            || ',"toolchainVersion":'
                            || to_json(package_row."ToolchainVersion")::text
                            || ',"contentHash":'
                            || to_json(package_row."ContentHash")::text
                            || ',"provenance":'
                            || to_json(package_row."Provenance")::text
                            || ',"signatureStatus":'
                            || package_row."SignatureStatus"::text
                            || ',"signatureAlgorithm":'
                            || coalesce(
                                to_json(package_row."SignatureAlgorithm")::text,
                                'null')
                            || ',"signatureValue":'
                            || coalesce(
                                to_json(package_row."SignatureValue")::text,
                                'null')
                            || ',"occurredAt":'
                            || to_json(occurred_at_text)::text
                            || '}';
                    ELSE
                        payload :=
                            '{"name":' || to_json(package_row."Name")::text
                            || ',"version":'
                            || to_json(package_row."Version")::text
                            || ',"buildIsolation":'
                            || package_row."BuildIsolation"::text
                            || ',"allowNetwork":'
                            || package_row."AllowNetwork"::text
                            || ',"timeoutTicks":'
                            || package_row."TimeoutTicks"::text
                            || ',"occurredAt":'
                            || to_json(occurred_at_text)::text
                            || '}';
                    END IF;

                    idempotency_key :=
                        event_type
                        || '|'
                        || package_row."Name"
                        || '|'
                        || package_row."Version"
                        || '|'
                        || occurred_at_ticks::text;

                    INSERT INTO package_outbox (
                        "IdempotencyKey",
                        "PackageName",
                        "PackageVersion",
                        "EventType",
                        "Payload",
                        "OccurredAt",
                        "AvailableAt",
                        "ClaimedAt",
                        "ClaimId",
                        "AttemptCount",
                        "Status",
                        "LastError"
                    )
                    VALUES (
                        idempotency_key,
                        package_row."Name",
                        package_row."Version",
                        event_type,
                        payload,
                        occurred_at_text::timestamptz,
                        occurred_at_text::timestamptz,
                        NULL,
                        NULL,
                        0,
                        0,
                        NULL
                    )
                    ON CONFLICT ("IdempotencyKey") DO NOTHING;
                END LOOP;

                INSERT INTO pkgsrv_processed_packages ("Name", "Version")
                VALUES (package_row."Name", package_row."Version");
            END LOOP;

            UPDATE packages AS package_row
            SET "PendingEventsJson" = '[]'
            FROM pkgsrv_processed_packages AS processed
            WHERE package_row."Name" = processed."Name"
              AND package_row."Version" = processed."Version";
        END
        $pkgsrv$;
        """;
}
