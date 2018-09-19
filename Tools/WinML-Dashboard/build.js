var electronInstaller = require('electron-winstaller');
var path = require("path");

resultPromise = electronInstaller.createWindowsInstaller({
    appDirectory: path.join('D:/myapp/WinmlDashboard-win32-x64'), 
    author: "Microsoft Corporation",
    exe: 'WinmlDashboard.exe', 
    noMsi: true, 
    outputDirectory: path.join('D:/tmp/build/installer64'),
  });

// tslint:disable-next-line:no-console
resultPromise.then(() => console.log("It worked!"), (e) => console.log(`No dice: ${e.message}`));