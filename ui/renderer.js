// Simple renderer.js
console.log("Simple renderer starting");

// Wait until DOM is fully loaded
window.addEventListener('DOMContentLoaded', () => {
  console.log("DOM fully loaded and parsed");
  
  // Log all available buttons
  const allButtons = document.querySelectorAll('button');
  console.log(`Found ${allButtons.length} buttons in the document`);
  
  // Log electronAPI
  console.log("electronAPI available:", window.electronAPI ? "Yes" : "No");
  if (window.electronAPI) {
    console.log("Available electronAPI methods:", Object.keys(window.electronAPI));
  }
  
  // Set up tab navigation
  setupTabs();
  
  // Set up buttons with direct click handlers
  setupButtons();
  
  // Set up system info
  setupSystemInfo();
  
  // Set up IPC listeners
  setupIPCListeners();
});

// Global variables
let workers = [];
let isEncrypt = true;

// Set up tab navigation
function setupTabs() {
  const tabButtons = document.querySelectorAll('.nav-tab');
  const tabContents = document.querySelectorAll('.tab-content');
  
  console.log(`Found ${tabButtons.length} tab buttons and ${tabContents.length} tab contents`);
  
  tabButtons.forEach(button => {
    button.onclick = function() {
      const tabId = this.getAttribute('data-tab');
      console.log(`Tab clicked: ${tabId}`);
      
      // Deactivate all tabs
      tabButtons.forEach(btn => btn.classList.remove('active'));
      tabContents.forEach(content => content.classList.remove('active'));
      
      // Activate selected tab
      this.classList.add('active');
      const content = document.getElementById(tabId);
      if (content) {
        content.classList.add('active');
      }
    };
  });
}

// Set up buttons with direct click handlers
function setupButtons() {
  // Start worker button
  const startWorkerBtn = document.getElementById('startWorkerBtn');
  if (startWorkerBtn) {
    startWorkerBtn.onclick = function() {
      const addressInput = document.getElementById('workerAddress');
      const address = addressInput ? addressInput.value.trim() : '127.0.0.1:50051';
      console.log(`Starting worker at ${address}`);
      
      if (window.electronAPI && window.electronAPI.startWorker) {
        window.electronAPI.startWorker(address);
      }
    };
    console.log("Attached click handler to startWorkerBtn");
  }
  
  // Browse input file button
  const browseInputBtn = document.getElementById('browseInputFile');
  if (browseInputBtn) {
    browseInputBtn.onclick = async function() {
      console.log("Browse input file clicked");
      if (window.electronAPI && window.electronAPI.openFileDialog) {
        try {
          const filePath = await window.electronAPI.openFileDialog();
          if (filePath) {
            const inputFilePath = document.getElementById('inputFilePath');
            if (inputFilePath) {
              inputFilePath.value = filePath;
              updateOutputPath();
            }
          }
        } catch (error) {
          console.error("Error browsing for input file:", error);
        }
      }
    };
    console.log("Attached click handler to browseInputBtn");
  }
  
  // Browse output file button
  const browseOutputBtn = document.getElementById('browseOutputFile');
  if (browseOutputBtn) {
    browseOutputBtn.onclick = async function() {
      console.log("Browse output file clicked");
      if (window.electronAPI && window.electronAPI.saveFileDialog) {
        try {
          const filePath = await window.electronAPI.saveFileDialog();
          if (filePath) {
            const outputFilePath = document.getElementById('outputFilePath');
            if (outputFilePath) {
              outputFilePath.value = filePath;
            }
          }
        } catch (error) {
          console.error("Error browsing for output file:", error);
        }
      }
    };
    console.log("Attached click handler to browseOutputBtn");
  }
  
  // Encrypt/Decrypt toggle buttons
  const encryptBtn = document.getElementById('encryptBtn');
  const decryptBtn = document.getElementById('decryptBtn');
  const processFileBtn = document.getElementById('processFileBtn');
  
  if (encryptBtn) {
    encryptBtn.onclick = function() {
      encryptBtn.classList.add('active');
      if (decryptBtn) decryptBtn.classList.remove('active');
      if (processFileBtn) processFileBtn.textContent = 'Encrypt File';
      isEncrypt = true;
      updateOutputPath();
    };
    console.log("Attached click handler to encryptBtn");
  }
  
  if (decryptBtn) {
    decryptBtn.onclick = function() {
      decryptBtn.classList.add('active');
      if (encryptBtn) encryptBtn.classList.remove('active');
      if (processFileBtn) processFileBtn.textContent = 'Decrypt File';
      isEncrypt = false;
      updateOutputPath();
    };
    console.log("Attached click handler to decryptBtn");
  }
  
  // Process file button
  if (processFileBtn) {
    processFileBtn.onclick = function() {
      const inputFilePath = document.getElementById('inputFilePath');
      const outputFilePath = document.getElementById('outputFilePath');
      
      if (!inputFilePath || !inputFilePath.value) {
        console.error("Input file is required");
        return;
      }
      
      const input = inputFilePath.value;
      const output = outputFilePath ? outputFilePath.value : '';
      
      const selectedWorkers = workers.filter(w => w.selected);
      if (selectedWorkers.length === 0) {
        console.error("No workers selected");
        return;
      }
      
      console.log(`Processing file: ${isEncrypt ? 'encrypt' : 'decrypt'}, ${input} → ${output}`);
      
      if (window.electronAPI) {
        if (isEncrypt && window.electronAPI.encryptFile) {
          window.electronAPI.encryptFile(input, output, selectedWorkers.map(w => w.id));
        } else if (!isEncrypt && window.electronAPI.decryptFile) {
          window.electronAPI.decryptFile(input, output, selectedWorkers.map(w => w.id));
        }
      }
      
      // Show progress indicator
      const progressContainer = document.getElementById('progressContainer');
      if (progressContainer) {
        progressContainer.style.display = 'flex';
      }
      
      const statusMessage = document.getElementById('statusMessage');
      if (statusMessage) {
        statusMessage.textContent = `${isEncrypt ? 'Encrypting' : 'Decrypting'} file...`;
      }
    };
    console.log("Attached click handler to processFileBtn");
  }
  
  // Dropbox configuration button
  const configureDropboxBtn = document.getElementById('configureDropboxBtn');
  if (configureDropboxBtn) {
    configureDropboxBtn.onclick = async function() {
      console.log("Configure Dropbox clicked");
      
      const tokenInput = document.getElementById('dropboxToken');
      const folderInput = document.getElementById('dropboxFolder');
      
      if (!tokenInput || !tokenInput.value.trim()) {
        alert("Dropbox access token is required");
        return;
      }
      
      const token = tokenInput.value.trim();
      const folder = folderInput ? folderInput.value.trim() : '/encryption_files';
      
      if (window.electronAPI && window.electronAPI.configureDropbox) {
        try {
          // Show progress
          configureDropboxBtn.textContent = "Saving...";
          configureDropboxBtn.disabled = true;
          
          const result = await window.electronAPI.configureDropbox(token, folder);
          
          // Reset button
          configureDropboxBtn.textContent = "Save Configuration";
          configureDropboxBtn.disabled = false;
          
          if (result.success) {
            alert("Dropbox configuration saved successfully!");
          } else {
            alert("Failed to save Dropbox configuration: " + result.message);
          }
        } catch (error) {
          console.error("Error configuring Dropbox:", error);
          configureDropboxBtn.textContent = "Save Configuration";
          configureDropboxBtn.disabled = false;
          alert("Error configuring Dropbox: " + error.message);
        }
      }
    };
    console.log("Attached click handler to configureDropboxBtn");
  }
  
  // Browse local file button for Dropbox operations
  const browseLocalFile = document.getElementById('browseLocalFile');
  if (browseLocalFile) {
    browseLocalFile.onclick = async function() {
      console.log("Browse local file for Dropbox clicked");
      if (window.electronAPI && window.electronAPI.openFileDialog) {
        try {
          const filePath = await window.electronAPI.openFileDialog();
          if (filePath) {
            const localFilePath = document.getElementById('localFilePath');
            if (localFilePath) {
              localFilePath.value = filePath;
              
              // Auto-populate dropbox path
              const dropboxPath = document.getElementById('dropboxPath');
              if (dropboxPath) {
                // Extract filename from path
                const filename = filePath.split(/[\\/]/).pop();
                dropboxPath.value = `/encryption_files/${filename}`;
              }
            }
          }
        } catch (error) {
          console.error("Error browsing for local file:", error);
        }
      }
    };
    console.log("Attached click handler to browseLocalFile");
  }
  
  // Upload to Dropbox button
  const uploadFileBtn = document.getElementById('uploadFileBtn');
  if (uploadFileBtn) {
    uploadFileBtn.onclick = async function() {
      console.log("Upload to Dropbox clicked");
      
      const localFilePath = document.getElementById('localFilePath');
      const dropboxPath = document.getElementById('dropboxPath');
      
      if (!localFilePath || !localFilePath.value.trim()) {
        alert("Local file path is required");
        return;
      }
      
      if (!dropboxPath || !dropboxPath.value.trim()) {
        alert("Dropbox path is required");
        return;
      }
      
      if (window.electronAPI && window.electronAPI.uploadToDropbox) {
        try {
          // Show progress
          uploadFileBtn.textContent = "Uploading...";
          uploadFileBtn.disabled = true;
          
          const result = await window.electronAPI.uploadToDropbox(
            localFilePath.value.trim(), 
            dropboxPath.value.trim()
          );
          
          // Reset button
          uploadFileBtn.textContent = "Upload File";
          uploadFileBtn.disabled = false;
          
          if (result.success) {
            alert("File uploaded successfully to Dropbox!");
          } else {
            alert("Failed to upload file to Dropbox: " + result.message);
          }
        } catch (error) {
          console.error("Error uploading to Dropbox:", error);
          uploadFileBtn.textContent = "Upload File";
          uploadFileBtn.disabled = false;
          alert("Error uploading to Dropbox: " + error.message);
        }
      }
    };
    console.log("Attached click handler to uploadFileBtn");
  }
  
  // Download from Dropbox button
  const downloadFileBtn = document.getElementById('downloadFileBtn');
  if (downloadFileBtn) {
    downloadFileBtn.onclick = async function() {
      console.log("Download from Dropbox clicked");
      
      const localFilePath = document.getElementById('localFilePath');
      const dropboxPath = document.getElementById('dropboxPath');
      
      if (!localFilePath || !localFilePath.value.trim()) {
        alert("Local file path is required");
        return;
      }
      
      if (!dropboxPath || !dropboxPath.value.trim()) {
        alert("Dropbox path is required");
        return;
      }
      
      if (window.electronAPI && window.electronAPI.downloadFromDropbox) {
        try {
          // Show progress
          downloadFileBtn.textContent = "Downloading...";
          downloadFileBtn.disabled = true;
          
          const result = await window.electronAPI.downloadFromDropbox(
            dropboxPath.value.trim(),
            localFilePath.value.trim()
          );
          
          // Reset button
          downloadFileBtn.textContent = "Download File";
          downloadFileBtn.disabled = false;
          
          if (result.success) {
            alert("File downloaded successfully from Dropbox!");
          } else {
            alert("Failed to download file from Dropbox: " + result.message);
          }
        } catch (error) {
          console.error("Error downloading from Dropbox:", error);
          downloadFileBtn.textContent = "Download File";
          downloadFileBtn.disabled = false;
          alert("Error downloading from Dropbox: " + error.message);
        }
      }
    };
    console.log("Attached click handler to downloadFileBtn");
  }
}

// Set up system info
function setupSystemInfo() {
  if (window.electronAPI) {
    const hostnameSpan = document.getElementById('hostname');
    const cpuCountSpan = document.getElementById('cpu-count');
    
    if (hostnameSpan && window.electronAPI.getHostname) {
      hostnameSpan.textContent = `Host: ${window.electronAPI.getHostname()}`;
    }
    
    if (cpuCountSpan && window.electronAPI.getCPUCount) {
      cpuCountSpan.textContent = `CPUs: ${window.electronAPI.getCPUCount()}`;
    }
  }
}

// Set up IPC listeners
function setupIPCListeners() {
  if (!window.electronAPI || !window.electronAPI.on) {
    console.error("electronAPI.on is not available");
    return;
  }
  
  // Worker events
  window.electronAPI.on('worker-started-result', (data) => {
    console.log("Worker started:", data);
    if (data && typeof data.id !== 'undefined') {
      const worker = {
        id: data.id,
        address: data.address,
        status: 'running',
        selected: true
      };
      workers.push(worker);
      updateWorkersUI();
    }
  });
  
  window.electronAPI.on('worker-stopped', (data) => {
    console.log("Worker stopped:", data);
    if (data && typeof data.id !== 'undefined') {
      workers = workers.filter(w => w.id !== data.id);
      updateWorkersUI();
    }
  });
  
  // Process events
  window.electronAPI.on('process-output', (data) => {
    console.log("Process output:", data);
    if (data && data.output) {
      appendToLog(data.output);
      
      // Check for progress updates
      if (data.output.includes('Progress:')) {
        const match = data.output.match(/Progress:\s+(\d+)%/);
        if (match && match[1]) {
          updateProgress(parseInt(match[1]));
        }
      }
    }
  });
  
  window.electronAPI.on('process-warning', (data) => {
    console.log("Process warning:", data);
    if (data && data.output) {
      appendToLog(data.output, true, 'warning');
    }
  });
  
  window.electronAPI.on('process-error', (data) => {
    console.log("Process error:", data);
    if (data && data.output) {
      appendToLog(data.output, true);
    }
  });
  
  window.electronAPI.on('process-completed', (data) => {
    console.log("Process completed:", data);
    const code = data && typeof data.code !== 'undefined' ? data.code : 0;
    
    const statusMessage = document.getElementById('statusMessage');
    if (statusMessage) {
      statusMessage.textContent = code === 0 
        ? "File processing completed successfully!" 
        : `File processing failed with code: ${code}`;
    }
    
    const progressContainer = document.getElementById('progressContainer');
    if (progressContainer) {
      if (code === 0) {
        updateProgress(100);
        
        // Hide progress after a delay
        setTimeout(() => {
          progressContainer.style.display = 'none';
        }, 3000);
      }
    }
    
    appendToLog(`Process completed with exit code: ${code}`);
  });
}

// Update the workers UI
function updateWorkersUI() {
  const workersList = document.getElementById('workersList');
  if (!workersList) return;
  
  if (workers.length === 0) {
    workersList.innerHTML = '<div class="empty-state">No active workers</div>';
    return;
  }
  
  workersList.innerHTML = '';
  workers.forEach(worker => {
    const workerItem = document.createElement('div');
    workerItem.className = 'worker-item';
    workerItem.innerHTML = `
      <div class="worker-info">
        <span class="worker-id">Worker #${worker.id}</span>
        <span class="worker-address">${worker.address}</span>
        <span class="worker-status ${worker.status}">${worker.status}</span>
      </div>
      <button class="btn stop-worker-btn" data-id="${worker.id}">Stop</button>
    `;
    workersList.appendChild(workerItem);
    
    // Add stop button click handler
    const stopBtn = workerItem.querySelector('.stop-worker-btn');
    if (stopBtn && window.electronAPI && window.electronAPI.stopWorker) {
      stopBtn.onclick = function() {
        window.electronAPI.stopWorker(worker.id);
      };
    }
  });
  
  // Update worker selection in file operations tab
  updateWorkerSelection();
}

// Update worker selection in file operations tab
function updateWorkerSelection() {
  const workerSelection = document.getElementById('workerSelection');
  if (!workerSelection) return;
  
  if (workers.length === 0) {
    workerSelection.innerHTML = '<p class="empty-state">No workers available. Please start workers in the Worker Management tab.</p>';
    return;
  }
  
  workerSelection.innerHTML = '';
  workers.forEach(worker => {
    if (worker.status === 'running') {
      const checkbox = document.createElement('div');
      checkbox.className = 'worker-checkbox';
      checkbox.innerHTML = `
        <input type="checkbox" id="worker-${worker.id}" ${worker.selected ? 'checked' : ''}>
        <label for="worker-${worker.id}">Worker #${worker.id} (${worker.address})</label>
      `;
      workerSelection.appendChild(checkbox);
      
      // Add checkbox change handler
      const input = checkbox.querySelector('input');
      if (input) {
        input.onchange = function() {
          worker.selected = this.checked;
          updateProcessButton();
        };
      }
    }
  });
  
  updateProcessButton();
}

// Update output path based on input path
function updateOutputPath() {
  const inputFilePath = document.getElementById('inputFilePath');
  const outputFilePath = document.getElementById('outputFilePath');
  
  if (!inputFilePath || !outputFilePath || !inputFilePath.value) return;
  
  const inputPath = inputFilePath.value;
  let outputPath = '';
  
  if (isEncrypt) {
    // For encryption, add .encrypted extension
    outputPath = inputPath + '.encrypted';
  } else {
    // For decryption, remove .encrypted extension if present
    if (inputPath.endsWith('.encrypted')) {
      outputPath = inputPath.substring(0, inputPath.lastIndexOf('.encrypted'));
    } else {
      // If no .encrypted extension, add .decrypted to the filename
      const lastDotIndex = inputPath.lastIndexOf('.');
      if (lastDotIndex !== -1) {
        const extension = inputPath.substring(lastDotIndex);
        const basePath = inputPath.substring(0, lastDotIndex);
        outputPath = basePath + '.decrypted' + extension;
      } else {
        outputPath = inputPath + '.decrypted';
      }
    }
  }
  
  outputFilePath.value = outputPath;
  updateProcessButton();
}

// Update process button state
function updateProcessButton() {
  const processFileBtn = document.getElementById('processFileBtn');
  const inputFilePath = document.getElementById('inputFilePath');
  
  if (!processFileBtn || !inputFilePath) return;
  
  const hasInputFile = inputFilePath.value.trim() !== '';
  const hasWorkers = workers.some(worker => worker.selected);
  
  processFileBtn.disabled = !(hasInputFile && hasWorkers);
}

// Update progress bar
function updateProgress(percent) {
  const progressBar = document.getElementById('progressBar');
  const progressText = document.getElementById('progressText');
  
  if (progressBar) {
    progressBar.style.width = `${percent}%`;
  }
  
  if (progressText) {
    progressText.textContent = `${percent}%`;
  }
}

// Function to append messages to the log
function appendToLog(message, isError = false, type = 'info') {
  const logArea = document.getElementById('logOutput');
  if (!logArea) return;
  
  const logEntry = document.createElement('div');
  logEntry.classList.add('log-entry');
  
  if (isError) {
    logEntry.classList.add('error');
  } else if (type === 'warning') {
    logEntry.classList.add('warning');
  }
  
  logEntry.textContent = message;
  logArea.appendChild(logEntry);
  logArea.scrollTop = logArea.scrollHeight; // Auto-scroll to bottom
} 