namespace Pkgsrv.Infrastructure.Postgres;

public static class PostgresPackageSql
{
    public const int RequiredSchemaVersion = 1;

    public const string Columns =
        "name, version, version_major, version_minor, version_patch, "
        + "version_pre_release, version_build_metadata, source_url, source_commit, "
        + "toolchain_name, toolchain_version, content_hash, provenance, "
        + "signature_status, signature_algorithm, signature_value, "
        + "isolation, allow_network, timeout_ticks, state, registered_at, build_requested_at";

    public const string Find =
        "SELECT " + Columns
        + " FROM pkgsrv.packages WHERE name = @name AND version = @version";

    public const string Insert = """
        INSERT INTO pkgsrv.packages (
            name, version, version_major, version_minor, version_patch,
            version_pre_release, version_build_metadata, source_url, source_commit,
            toolchain_name, toolchain_version, content_hash, provenance,
            signature_status, signature_algorithm, signature_value,
            isolation, allow_network, timeout_ticks, state, build_requested_at, registered_at
        )
        VALUES (
            @name, @version, @version_major, @version_minor, @version_patch,
            @version_pre_release, @version_build_metadata, @source_url, @source_commit,
            @toolchain_name, @toolchain_version, @content_hash, @provenance,
            @signature_status, @signature_algorithm, @signature_value,
            @isolation, @allow_network, @timeout_ticks, @state, @build_requested_at, @registered_at
        )
        ON CONFLICT (name, version) DO NOTHING
        """;

    public const string SelectStateForUpdate =
        "SELECT state FROM pkgsrv.packages WHERE name = @name AND version = @version FOR UPDATE";

    public const string UpdateState = """
        UPDATE pkgsrv.packages
        SET state = @state, build_requested_at = @build_requested_at
        WHERE name = @name AND version = @version
        """;

    public const string Search = """
        SELECT
            name, version, version_major, version_minor, version_patch,
            version_pre_release, version_build_metadata, source_url, source_commit,
            toolchain_name, toolchain_version, content_hash, provenance,
            signature_status, signature_algorithm, signature_value,
            isolation, allow_network, timeout_ticks, state, registered_at, build_requested_at
        FROM pkgsrv.packages
        WHERE @contains IS NULL
           OR name ILIKE @contains ESCAPE '\'
           OR source_commit ILIKE @prefix ESCAPE '\'
           OR provenance ILIKE @contains ESCAPE '\'
        ORDER BY
            name COLLATE "C" ASC,
            version_major ASC,
            version_minor ASC,
            version_patch ASC,
            version_pre_release COLLATE "C" ASC NULLS LAST,
            version_build_metadata COLLATE "C" ASC NULLS FIRST,
            version COLLATE "C" ASC
        LIMIT @limit
        """;

    public const string SchemaVersion = "SELECT COALESCE(MAX(version), 0) FROM pkgsrv.schema_versions";

    public const string SchemaCheck = "SELECT to_regclass('pkgsrv.packages')";
}
