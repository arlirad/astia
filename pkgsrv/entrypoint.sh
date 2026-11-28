#!/bin/sh
set -eu

socket_path="${Pkgsrv__Listen__UnixSocketPath:-/run/pkgsrv/pkgsrv.sock}"
socket_dir="$(dirname "${socket_path}")"

if [ ! -d "${socket_dir}" ]; then
    echo "pkgsrv: socket directory does not exist: ${socket_dir}" >&2
    exit 1
fi

if [ ! -w "${socket_dir}" ]; then
    echo "pkgsrv: socket directory is not writable by uid $(id -u): ${socket_dir}" >&2
    exit 1
fi

if [ -e "${socket_path}" ] || [ -L "${socket_path}" ]; then
    if [ -L "${socket_path}" ] || [ ! -S "${socket_path}" ]; then
        echo "pkgsrv: refusing to remove non-socket path: ${socket_path}" >&2
        exit 1
    fi

    owner="$(stat -c '%u' -- "${socket_path}")"
    if [ "${owner}" != "$(id -u)" ]; then
        echo "pkgsrv: refusing to remove socket owned by uid ${owner}: ${socket_path}" >&2
        exit 1
    fi

    rm -f -- "${socket_path}"
fi

exec dotnet /app/Pkgsrv.Presentation.dll "$@"
