using Microsoft.EntityFrameworkCore;

namespace Pkgsrv.Infrastructure.Persistence.Postgres;

public sealed class PostgresPackageDbContext : DbContext
{
    public PostgresPackageDbContext(DbContextOptions<PostgresPackageDbContext> options)
        : base(options)
    {
    }

    public DbSet<PackageRecord> Packages => Set<PackageRecord>();

    public DbSet<PackageOutboxRecord> PackageOutbox => Set<PackageOutboxRecord>();

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        var package = modelBuilder.Entity<PackageRecord>();

        package.ToTable("packages");
        package.HasKey(record => new { record.Name, record.Version });
        package.Property(record => record.Name).IsRequired();
        package.Property(record => record.Version).IsRequired();
        package.Property(record => record.SourceUrl).IsRequired();
        package.Property(record => record.SourceCommit).IsRequired();
        package.Property(record => record.ToolchainName).IsRequired();
        package.Property(record => record.ToolchainVersion).IsRequired();
        package.Property(record => record.ContentHash).IsRequired();
        package.Property(record => record.Provenance).IsRequired();
        package.Property(record => record.PendingEventsJson).IsRequired();
        package.Property(record => record.RegisteredAt).HasColumnType("timestamp with time zone");
        package.Property(record => record.BuildRequestedAt).HasColumnType("timestamp with time zone");
        package.Property(record => record.PublishedAt).HasColumnType("timestamp with time zone");
        package.HasIndex(record => record.Name).HasDatabaseName("IX_packages_Name");
        package.HasIndex(record => record.State).HasDatabaseName("IX_packages_State");
        package.HasIndex(record => record.RegisteredAt).HasDatabaseName("IX_packages_RegisteredAt");
        package.HasIndex(record => new
        {
            record.Name,
            record.VersionMajor,
            record.VersionMinor,
            record.VersionPatch,
            record.VersionPreReleaseSortKey,
            record.Version
        })
            .HasDatabaseName("IX_packages_VersionOrder");

        var outbox = modelBuilder.Entity<PackageOutboxRecord>();

        outbox.ToTable("package_outbox");
        outbox.HasKey(record => record.IdempotencyKey);
        outbox.Property(record => record.IdempotencyKey).IsRequired();
        outbox.Property(record => record.PackageName).IsRequired();
        outbox.Property(record => record.PackageVersion).IsRequired();
        outbox.Property(record => record.EventType).IsRequired();
        outbox.Property(record => record.Payload).IsRequired();
        outbox.Property(record => record.OccurredAt).HasColumnType("timestamp with time zone");
        outbox.Property(record => record.AvailableAt).HasColumnType("timestamp with time zone");
        outbox.Property(record => record.ClaimedAt).HasColumnType("timestamp with time zone");
        outbox.HasIndex(record => record.PackageName)
            .HasDatabaseName("IX_package_outbox_PackageName");
        outbox.HasIndex(record => new { record.Status, record.AvailableAt })
            .HasDatabaseName("IX_package_outbox_Status_AvailableAt");

        base.OnModelCreating(modelBuilder);
    }
}
