// preload.js
const { contextBridge, ipcRenderer } = require('electron');
const os = require('os');

console.log('Preload script starting');

// Create APIs and expose them to the renderer process
try {
  contextBridge.exposeInMainWorld('electronAPI', {
    // Worker management
    startWorker: (address) => {
      console.log(`Sending start-worker with address: ${address}`);
      ipcRenderer.send('start-worker', address);
    },
    
    stopWorker: (workerId) => {
      console.log(`Sending stop-worker with ID: ${workerId}`);
      ipcRenderer.send('stop-worker', workerId);
    },
    
    // File operations
    encryptFile: (inputFile, outputFile, workerIds) => {
      console.log(`Sending encrypt-file: ${inputFile} → ${outputFile}`);
      ipcRenderer.send('encrypt-file', inputFile, outputFile, workerIds);
    },
    
    decryptFile: (inputFile, outputFile, workerIds) => {
      console.log(`Sending decrypt-file: ${inputFile} → ${outputFile}`);
      ipcRenderer.send('decrypt-file', inputFile, outputFile, workerIds);
    },
    
    // Dialog
    openFileDialog: () => {
      console.log('Invoking open-file-dialog');
      return ipcRenderer.invoke('open-file-dialog');
    },
    
    saveFileDialog: () => {
      console.log('Invoking save-file-dialog');
      return ipcRenderer.invoke('save-file-dialog');
    },
    
    // Dropbox operations
    configureDropbox: (token, folder) => {
      console.log('Sending configure-dropbox');
      return ipcRenderer.invoke('configure-dropbox', token, folder);
    },
    
    uploadToDropbox: (localFile, dropboxPath) => {
      console.log(`Sending upload-to-dropbox: ${localFile} → ${dropboxPath}`);
      return ipcRenderer.invoke('upload-to-dropbox', localFile, dropboxPath);
    },
    
    downloadFromDropbox: (dropboxPath, localFile) => {
      console.log(`Sending download-from-dropbox: ${dropboxPath} → ${localFile}`);
      return ipcRenderer.invoke('download-from-dropbox', dropboxPath, localFile);
    },
    
    // System information
    getHostname: () => os.hostname(),
    getCPUCount: () => os.cpus().length,
    
    // Event listeners
    on: (channel, callback) => {
      console.log(`Setting up listener for channel: ${channel}`);
      
      const validChannels = [
        'worker-started-result',
        'worker-stopped',
        'worker-stopping',
        'worker-output', 
        'worker-error',
        'process-output',
        'process-error',
        'process-warning',
        'process-completed'
      ];
      
      if (validChannels.includes(channel)) {
        // Add the listener
        ipcRenderer.on(channel, (_, data) => {
          console.log(`Received message on ${channel}:`, data);
          callback(data);
        });
        return true;
      }
      
      console.error(`Invalid channel: ${channel}`);
      return false;
    }
  });
  
  console.log('Preload script completed successfully');
} catch (error) {
  console.error('Error in preload script:', error);
} 