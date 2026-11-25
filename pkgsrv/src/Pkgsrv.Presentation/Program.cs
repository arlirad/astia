using Pkgsrv.Presentation;

var app = PkgsrvHost.Create(args);
await PkgsrvHost.InitializeAsync(app);
app.Run();
