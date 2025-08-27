let currentPath = '/';

function buildBreadcrumbs() {
  const container = document.getElementById('breadcrumbs');
  const parts = currentPath.split('/').filter(p => p);
  let path = '';
  container.innerHTML = `<a class="folder-link" onclick="navigateTo('/')">Root</a>`;
  parts.forEach((part) => {
    path += '/' + part;
    container.innerHTML += ' / <a class="folder-link" onclick="navigateTo(\'' + path + '/\')">' + part + '</a>';
  });
}

async function refreshFileList() {
  buildBreadcrumbs();
  const res = await fetch('/dav' + currentPath, {
    method: 'PROPFIND',
    headers: { 'Depth': '1' }
  });

  if (!res.ok) {
    alert('Fehler beim Laden: ' + await res.text());
    return;
  }

  const text = await res.text();
  const parser = new DOMParser();
  const xml = parser.parseFromString(text, 'application/xml');
  const responses = Array.from(xml.getElementsByTagName('d:response'));

  const tbody = document.querySelector("#fileList tbody");
  tbody.innerHTML = '';

  const currentHref = decodeURIComponent(new URL('/dav' + currentPath, window.location.origin).pathname);
  const dirs = [];
  const files = [];
  
  let first = true;
	
  responses.forEach(item => {
    let href = item.getElementsByTagName('d:href')[0].textContent;
    href = decodeURIComponent(href);

	if(first){
		first = false;
	}else{
		const name = href.endsWith('/') ? href.slice(0, -1).split('/').pop() : href.split('/').pop();
		const isDir = item.getElementsByTagName('d:collection').length > 0;
		const entry = { name, href, isDir };
		(isDir ? dirs : files).push(entry);
	}
  });

  // ➤ Sortieren (alphabetisch, case-insensitive)
  const sortByName = (a, b) => a.name.toLowerCase().localeCompare(b.name.toLowerCase());
  dirs.sort(sortByName);
  files.sort(sortByName);

  // ➤ Anzeigen
  [...dirs, ...files].forEach(({ name, href, isDir }) => {
    const tr = document.createElement('tr');
    const fullPath = currentPath + name + (isDir ? '/' : '');

    if (isDir) {
      tr.innerHTML = `<td><a class='folder-link' onclick="navigateTo('${fullPath}')">📁 ${name}</a></td>
        <td><button onclick="deleteFile('${name}', true)" title="Löschen">🗑️</button>
            <button onclick="moveFilePrompt('${name}', true)" title="Verschieben">📂</button></td>`;
    } else {
		const editable = ['.txt', '.js', '.json','.css','.html'].some(ext => name.toLowerCase().endsWith(ext));
      
      tr.innerHTML = `<td>${name}</td><td>
        <button onclick="downloadFile('${name}')" title="Download">⬇️</button>
        <button onclick="deleteFile('${name}', false)" title="Löschen">🗑️ </button>
        <button onclick="renameFilePrompt('${name}')" title="Umbenennen">✏️</button>
        <button onclick="moveFilePrompt('${name}', false)" title="Verschieben">📂</button>
		${editable ? `<button onclick="editFile('${name}')" title="Bearbeiten">📝</button>` : ''}
      </td>`;
    }
//${editable ? `<button onclick="editFile('${name}')" title="Bearbeiten">📝</button>` : ''}
      
    tbody.appendChild(tr);
  });
}

function editFile(name) {
  const ext = name.toLowerCase().split('.').pop();
  const url = `/editor.html?file=${encodeURIComponent(currentPath + name)}&type=${ext}`;
  window.open(url, '_self');
}

function navigateTo(path) {
  currentPath = path.endsWith('/') ? path : path + '/';
  refreshFileList();
}

function downloadFile(name) {
  const a = document.createElement('a');
  a.href = '/dav' + currentPath + name;
  a.download = name;
  a.click();
}

async function deleteFile(name) {
  if (!confirm(`Datei wirklich löschen?\n${name}`)) return;
  await fetch('/dav' + currentPath + name, { method: 'DELETE' });
  refreshFileList();
}

async function renameFilePrompt(name) {
  const newName = prompt("Neuer Name:", name);
  if (!newName || newName === name) return;

  await fetch('/dav' + currentPath + name, {
    method: 'MOVE',
    headers: {
      Destination: '/dav' + currentPath + newName
    }
  });
  refreshFileList();
}

async function moveFilePrompt(name, isDir) {
  if (isDir) {
    alert("Ordner-Verschiebung nicht unterstützt.");
    return;
  }
  const dest = prompt("Zielverzeichnis (z. B. /sub/):", currentPath);
  if (!dest) return;

  const destPath = dest.endsWith('/') ? dest : dest + '/';
  await fetch('/dav' + currentPath + name, {
    method: 'MOVE',
    headers: {
      Destination: '/dav' + destPath + name
    }
  });
  refreshFileList();
}

async function newFolder() {
  const folderName = prompt("Neuer Ordner:");
  if (!folderName) return;
  await fetch('/dav' + currentPath + folderName + '/', { method: 'MKCOL' });
  refreshFileList();
}

function handleFileSelect(evt) {
  const files = evt.target.files;
  if (files.length) {
    Array.from(files).forEach(file => {
      uploadFile(file)
        .then(() => {
          refreshFileList();                 // Ansicht aktualisieren
          evt.target.value = '';             // Input resetten
        })
        .catch(err => alert(err.message));
    });
  }
}

/*
function handleFileSelect(evt) {
  const files = evt.target.files;
  for (let file of files) uploadFile(file);
}*/
/*
function uploadFile(file) {
  fetch('/dav' + currentPath + file.name, {
    method: 'PUT',
    body: file
  }).then(() => refreshFileList());
}*/

function initFileList(){
    const params = new URLSearchParams(location.search);
    const dir = params.get('dir');
	if (dir) currentPath = dir.endsWith('/') ? dir : dir + '/';
	refreshFileList();
}
/*
document.getElementById('uploadArea').addEventListener('drop', (e) => {
  e.preventDefault();
  const files = e.dataTransfer.files;
  for (let file of files) uploadFile(file);
});*/

function uploadFile(file) {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open('PUT', '/dav' + currentPath + file.name);

    // Fortschritt anzeigen
    const progressBar = document.getElementById('uploadProgress');
    if (progressBar) {
      progressBar.value = 0;
      progressBar.max = file.size;
      progressBar.style.display = 'block';
    }

    xhr.upload.onprogress = (event) => {
      if (progressBar && event.lengthComputable) {
        progressBar.value = event.loaded;
      }
    };

    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        if (progressBar) progressBar.style.display = 'none';
        resolve();
      } else {
        reject(new Error(`Upload fehlgeschlagen: ${xhr.status}`));
      }
    };

    xhr.onerror = () => reject(new Error('Netzwerkfehler beim Upload'));
    xhr.send(file);
  });
}

// Drag & Drop initialisieren
const uploadArea = document.getElementById('uploadArea');

// Notwendig, damit Drop funktioniert
uploadArea.addEventListener('dragover', (e) => {
  e.preventDefault();
  uploadArea.classList.add('dragover');
});
uploadArea.addEventListener('dragleave', () => {
  uploadArea.classList.remove('dragover');
});
uploadArea.addEventListener('drop', (e) => {
  e.preventDefault();
  uploadArea.classList.remove('dragover');
  const files = e.dataTransfer.files;
  if (files.length) {
    Array.from(files).forEach(file => {
      uploadFile(file)
        .then(() => refreshFileList())
        .catch(err => alert(err.message));
    });
  }
});



initFileList();
