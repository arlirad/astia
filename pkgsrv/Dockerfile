# syntax=docker/dockerfile:1

FROM mcr.microsoft.com/dotnet/sdk:10.0 AS build
WORKDIR /src

ARG IRVA_CLIENT_PROJECT=/src/vendor/irva/src/Irva.Client/Irva.Client.csproj

COPY Directory.Build.props ./
COPY src/ src/
COPY vendor/irva/ vendor/irva/

RUN dotnet restore src/Pkgsrv.Presentation/Pkgsrv.Presentation.csproj \
        -p:IrvaClientProject="${IRVA_CLIENT_PROJECT}"

RUN dotnet publish src/Pkgsrv.Presentation/Pkgsrv.Presentation.csproj \
        -c Release \
        --no-restore \
        -o /app \
        -p:IrvaClientProject="${IRVA_CLIENT_PROJECT}" \
        -p:UseAppHost=false

FROM mcr.microsoft.com/dotnet/aspnet:10.0 AS runtime
WORKDIR /app

ENV ASPNETCORE_ENVIRONMENT=Production \
    DOTNET_EnableDiagnostics=0

COPY --from=build /app ./
COPY entrypoint.sh /usr/local/bin/pkgsrv-entrypoint

RUN chmod 0755 /usr/local/bin/pkgsrv-entrypoint \
 && install -d -o "${APP_UID}" -g "${APP_UID}" -m 0750 /run/pkgsrv

USER ${APP_UID}

ENTRYPOINT ["/usr/local/bin/pkgsrv-entrypoint"]
