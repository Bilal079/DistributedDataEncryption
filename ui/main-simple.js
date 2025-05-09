// Simplified main.js for testing
const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const path = require('path');
const fs = require('fs');

// Global reference to the window object to prevent garbage collection
let mainWindow = null;

// Create the main application window with a very simple configuration
function createWindow() {
  console.log('Creating window');
  
  // Use absolute path for preload
  const preloadPath = path.resolve(__dirname, 'preload.js');
  console.log('Preload script path:', preloadPath);
  console.log('File exists check:', fs.existsSync(preloadPath) ? 'Found' : 'Not found');
  
  // Create a window with minimal settings
  mainWindow = new BrowserWindow({
    width: 800,
    height: 600,
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: false,
      preload: preloadPath
    }
  });
  
  // Load the test HTML file
  console.log('Loading test.html');
  mainWindow.loadFile(path.join(__dirname, 'test.html'));
  
  // Open DevTools for debugging
  mainWindow.webContents.openDevTools();
  
  // Log events for debugging
  mainWindow.webContents.on('did-finish-load', () => {
    console.log('Page loaded successfully');
  });
  
  mainWindow.webContents.on('did-fail-load', (event, errorCode, errorDescription) => {
    console.error(`Failed to load: ${errorDescription} (${errorCode})`);
  });
  
  mainWindow.on('closed', () => {
    console.log('Window closed');
    mainWindow = null;
  });
}

// Set up very basic IPC handlers
function setupBasicIPC() {
  console.log('Setting up basic IPC handlers');
  
  // Handler for starting a worker
  ipcMain.on('start-worker', (event, address) => {
    console.log(`Received start-worker request with address: ${address}`);
    
    // Send back a success response after a short delay
    setTimeout(() => {
      console.log('Sending worker-started-result response');
      event.sender.send('worker-started-result', { 
        id: Math.floor(Math.random() * 1000),
        address: address,
        success: true
      });
    }, 500);
  });
  
  // Handler for file dialog
  ipcMain.handle('open-file-dialog', async () => {
    console.log('Received open-file-dialog request');
    try {
      const result = await dialog.showOpenDialog(mainWindow, {
        properties: ['openFile']
      });
      console.log('Dialog result:', result);
      return result.canceled ? null : result.filePaths[0];
    } catch (error) {
      console.error('Error showing file dialog:', error);
      throw error;
    }
  });
  
  console.log('IPC handlers set up');
}

// App initialization
app.whenReady().then(() => {
  console.log('App is ready');
  createWindow();
  setupBasicIPC();
});

// Quit when all windows are closed, except on macOS
app.on('window-all-closed', () => {
  console.log('All windows closed');
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

// On macOS, recreate the window when the dock icon is clicked
app.on('activate', () => {
  console.log('App activated');
  if (mainWindow === null) {
    createWindow();
  }
});

// Log startup and any uncaught exceptions
console.log('Simple application starting');

process.on('uncaughtException', (error) => {
  console.error('Uncaught exception:', error);
}); 