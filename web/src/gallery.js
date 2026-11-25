import { EpdCmd, write, addLog } from './main.js';

let parsingFileList = false;
let fileListCount = 0;

let isDownloading = false;
let downloadBuffer = [];
let downloadTargetSize = 0;
let downloadReceivedSize = 0;
let downloadFileName = '';

function setGalleryStatus(text, hide = false) {
  const bar = document.getElementById('gallerystatus').parentElement;
  bar.style.display = 'block';
  document.getElementById('gallerystatus').innerText = text;
  if (hide) setTimeout(() => { bar.style.display = 'none'; }, 5000);
}

export function handleFileMessage(data) {
  if (isDownloading) {
    for (let i = 0; i < data.length; i++) downloadBuffer.push(data[i]);
    downloadReceivedSize += data.length;
    setGalleryStatus(`下载中: ${Math.round(downloadReceivedSize / downloadTargetSize * 100)}%`);
    if (downloadReceivedSize >= downloadTargetSize) {
      isDownloading = false;
      saveDownloadedFile();
    }
    return true;
  }

  if (parsingFileList && data.length === 8) {
    const total = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    const used = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    document.getElementById('fs-free').innerText = `${(used / 1024).toFixed(1)} / ${(total / 1024).toFixed(1)} KB`;
    return true;
  }

  if (parsingFileList && data.length > 8 && data[0] <= 2) {
    const type = data[0];
    const size = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    let nameRaw = data.slice(8);
    let nameEnd = nameRaw.indexOf(0);
    if (nameEnd === -1) nameEnd = nameRaw.length;
    const name = new TextDecoder().decode(nameRaw.slice(0, nameEnd));

    if (type === 1 && name !== '.' && name !== '..') {
      addFileToList(name, size);
      fileListCount++;
      document.getElementById('img-count').innerText = fileListCount;
    }
    return true;
  }

  return false;
}

export async function refreshGallery() {
  const list = document.getElementById('gallery-list');
  list.innerHTML = '';
  fileListCount = 0;
  if (isDownloading && downloadReceivedSize > 0) {
    addLog(`下载中止: 已收 ${downloadReceivedSize}/${downloadTargetSize} 字节`);
  }
  isDownloading = false;
  document.getElementById('img-count').innerText = 0;
  document.getElementById('fs-free').innerText = '...';

  parsingFileList = true;

  await write(EpdCmd.FSSTAT);

  setTimeout(async () => {
    const path = "/img";
    const encoder = new TextEncoder();
    const pathBytes = encoder.encode(path);
    await write(EpdCmd.DIR, pathBytes);
  }, 200);

  setTimeout(() => {
    parsingFileList = false;
    if (fileListCount === 0) {
      list.innerHTML = '<div style="text-align:center;padding:20px;color:#999">暂无文件</div>';
    }
  }, 5000);
}

function addFileToList(name, size) {
  const list = document.getElementById('gallery-list');
  const div = document.createElement('div');
  div.className = 'gallery-item';

  const sizeStr = (size / 1024).toFixed(1) + ' KB';

  const info = document.createElement('div');
  info.className = 'info';
  info.title = name;
  const icon = document.createElement('span');
  icon.className = 'icon';
  icon.textContent = '🖼️';
  info.appendChild(icon);
  info.appendChild(document.createTextNode(name));

  const sizeDiv = document.createElement('div');
  sizeDiv.className = 'size';
  sizeDiv.textContent = sizeStr;

  const actions = document.createElement('div');
  actions.className = 'actions';
  const showBtn = document.createElement('button');
  showBtn.className = 'secondary';
  showBtn.textContent = '显示';
  showBtn.addEventListener('click', () => showImage(name));
  const downloadBtn = document.createElement('button');
  downloadBtn.className = 'secondary';
  downloadBtn.textContent = '下载';
  downloadBtn.addEventListener('click', () => downloadFile(name, size));
  const deleteBtn = document.createElement('button');
  deleteBtn.className = 'primary';
  deleteBtn.textContent = '删除';
  deleteBtn.addEventListener('click', () => deleteFile(name));
  actions.append(showBtn, downloadBtn, deleteBtn);

  div.append(info, sizeDiv, actions);
  list.appendChild(div);
}

async function downloadFile(name, size) {
  downloadFileName = name;
  downloadTargetSize = size;
  downloadReceivedSize = 0;
  downloadBuffer = [];
  isDownloading = true;

  const path = "/img/" + name;
  await write(EpdCmd.DOWNLOAD, new TextEncoder().encode(path));
}

function saveDownloadedFile() {
  setGalleryStatus("下载完成", true);
  addLog(`下载完成: ${downloadFileName}`);
  const blob = new Blob([new Uint8Array(downloadBuffer)], { type: "application/octet-stream" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = downloadFileName;
  document.body.appendChild(a);
  a.click();
  setTimeout(() => {
    document.body.removeChild(a);
    window.URL.revokeObjectURL(url);
  }, 0);
}

async function showImage(name) {
  await write(EpdCmd.SHOW_IMAGE, new TextEncoder().encode(name));
  addLog(`已发送显示指令: ${name}`);
}

async function deleteFile(name) {
  if (!confirm(`确定要删除 ${name} 吗？`)) return;
  const path = "/img/" + name;
  await write(EpdCmd.DELETE, new TextEncoder().encode(path));
  setTimeout(refreshGallery, 500);
}

export async function formatStorage() {
  if (!confirm('警告：确定要格式化存储器吗？\n这将删除所有文件且无法恢复！')) return;
  await write(EpdCmd.FORMAT);
  setGalleryStatus("正在格式化...");
  setTimeout(() => {
    setGalleryStatus("格式化完成", true);
    refreshGallery();
  }, 2000);
}

async function uploadFile(name, data) {
  const filename = "/img/" + name;
  setGalleryStatus(`正在上传 ${name}...`);

  let nameBytes = new TextEncoder().encode(filename);
  let openPayload = new Uint8Array(1 + nameBytes.length);
  openPayload[0] = 0; // Sub-command: Open
  openPayload.set(nameBytes, 1);
  await write(EpdCmd.UPLOAD, openPayload);

  const mtu = parseInt(document.getElementById('mtusize').value) || 20;
  const maxData = mtu - 2;

  if (maxData <= 0) {
    alert("MTU too small");
    return;
  }

  for (let i = 0; i < data.length; i += maxData) {
    const chunk = data.slice(i, i + maxData);
    const p = new Uint8Array(1 + chunk.length);
    p[0] = 1;
    p.set(chunk, 1);
    await write(EpdCmd.UPLOAD, p, true);
    setGalleryStatus(`上传中: ${Math.round((i + chunk.length) / data.length * 100)}%`);
  }

  await write(EpdCmd.UPLOAD, [2]);

  setGalleryStatus("上传完成", true);
  setTimeout(refreshGallery, 1000);
}

export function uploadRle() {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.rle';
  input.onchange = async () => {
    const file = input.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = async (e) => {
      await uploadFile(file.name, new Uint8Array(e.target.result));
    };
    reader.readAsArrayBuffer(file);
  };
  input.click();
}

export async function uploadImage(name, processedData, ditherMode, totalPixels) {
  let data;
  if (ditherMode === 'threeColor') {
    const half = Math.floor(processedData.length / 2);
    const black = encodeRLE(processedData.slice(0, half), 1, totalPixels);
    const red = encodeRLE(processedData.slice(half), 1, totalPixels);
    data = new Uint8Array(black.length + red.length);
    data.set(black);
    data.set(red, black.length);
  } else {
    data = encodeRLE(processedData, ditherMode === 'fourColor' ? 2 : 1, totalPixels);
  }
  await uploadFile(name, data);
}

function encodeRLE(bytes, bpp, totalPixels) {
  const out = [];
  const mask = (1 << bpp) - 1;
  const ppb = Math.floor(7 / bpp);
  const cbits = 6 - bpp;
  const getPixel = (k) => (bytes[(k * bpp) >> 3] >> (8 - bpp - ((k * bpp) & 7))) & mask;

  let litCount = 0;
  let litByte = 0;
  const pushLiteral = (v) => {
    litByte |= v << (7 - bpp * (litCount + 1));
    if (++litCount == ppb) {
      out.push(litByte);
      litCount = 0;
      litByte = 0;
    }
  };

  let i = 0;
  while (i < totalPixels) {
    const v = getPixel(i);
    let j = i + 1;
    while (j < totalPixels && getPixel(j) == v) j++;
    let run = j - i;

    if (run >= 8) {
      while (litCount != 0) { pushLiteral(v); run--; }
      if (run >= 8) {
        let count = run - 8;
        const low = count & ((1 << cbits) - 1);
        count >>= cbits;
        out.push(0x80 | (v << (cbits + 1)) | ((count > 0 ? 1 : 0) << cbits) | low);
        while (count > 0) {
          const part = count & 0x7F;
          count >>= 7;
          out.push((count > 0 ? 0x80 : 0) | part);
        }
        i = j;
        continue;
      }
    }
    for (let t = 0; t < run; t++) pushLiteral(v);
    i = j;
  }
  if (litCount > 0) out.push(litByte);

  return new Uint8Array(out);
}
