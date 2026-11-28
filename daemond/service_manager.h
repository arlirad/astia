#pragma once

/* Native, non-shell service units.  Each /etc/daemond/<name>.yaml file is a
 * controlled YAML subset:
 *
 *   description: free text        # optional
 *   exec: /absolute/program
 *   args:
 *     - argument
 *   restart: no | always | on-failure
 *   restart-limit: 0..16          # defaults to 3 when restart is enabled
 *   working-directory: /absolute  # optional
 *   environment:
 *     NAME: value
 *
 * Comments, single-quoted and double-quoted scalars are accepted.  Anchors,
 * flow collections, multi-document streams and tabs are not.  Unknown keys,
 * duplicate exec keys and non-absolute exec paths reject the whole unit. */
int astia_service_manager(const char* unit_directory);
