// E-Reader local web UI logic: list, upload (with progress) and delete books.
const $ = (sel) => document.querySelector(sel);

const els = {
  storage: $("#storage"),
  count: $("#count"),
  books: $("#books"),
  drop: $("#drop"),
  file: $("#file"),
  pick: $("#pick"),
  status: $("#status"),
  progressWrap: $("#progressWrap"),
  progressBar: $("#progressBar"),
};

function fmtSize(bytes) {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / 1024 / 1024).toFixed(1) + " MB";
}

async function loadBooks() {
  try {
    const res = await fetch("/api/books");
    const data = await res.json();
    const type = data.storage === "sd" ? "microSD" : "flash";
    const free = data.space && data.space.free != null ? data.space.free : null;
    els.storage.textContent =
      free != null ? fmtSize(free) + " free \u00b7 " + type : "storage: " + type;
    renderBooks(data.books || []);
  } catch (e) {
    els.books.innerHTML = '<li class="empty">Could not reach the device.</li>';
  }
}

function renderBooks(books) {
  els.count.textContent = books.length ? "(" + books.length + ")" : "";
  if (!books.length) {
    els.books.innerHTML = '<li class="empty">No books yet — upload one above.</li>';
    return;
  }
  els.books.innerHTML = "";
  books.forEach((b) => {
    const li = document.createElement("li");
    const info = document.createElement("div");
    info.innerHTML =
      `<div class="book-name">${escapeHtml(b.name)}</div>` +
      `<div class="book-meta">${fmtSize(b.size)}</div>`;
    const del = document.createElement("button");
    del.className = "del";
    del.textContent = "Delete";
    del.onclick = () => deleteBook(b.path, b.name);
    li.append(info, del);
    els.books.appendChild(li);
  });
}

function escapeHtml(s) {
  return s.replace(/[&<>"']/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

async function deleteBook(path, name) {
  if (!confirm(`Delete "${name}"?`)) return;
  await fetch("/api/delete?path=" + encodeURIComponent(path), { method: "POST" });
  loadBooks();
}

// --- Uploads (one at a time, with a live progress bar) --------------------
function uploadFiles(fileList) {
  const files = Array.from(fileList);
  if (!files.length) return;
  let i = 0;

  const next = () => {
    if (i >= files.length) {
      els.status.textContent = "Done.";
      els.progressWrap.hidden = true;
      loadBooks();
      return;
    }
    const file = files[i++];
    const form = new FormData();
    form.append("file", file, file.name);

    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/upload");
    els.progressWrap.hidden = false;
    els.status.textContent = `Uploading ${file.name}…`;

    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) {
        els.progressBar.style.width = (e.loaded / e.total) * 100 + "%";
      }
    };
    xhr.onload = () => {
      els.progressBar.style.width = "0%";
      next();
    };
    xhr.onerror = () => {
      els.status.textContent = "Upload failed for " + file.name;
    };
    xhr.send(form);
  };
  next();
}

// --- Wire up drag & drop + file picker ------------------------------------
els.pick.onclick = () => els.file.click();
els.file.onchange = () => uploadFiles(els.file.files);

["dragenter", "dragover"].forEach((ev) =>
  els.drop.addEventListener(ev, (e) => {
    e.preventDefault();
    els.drop.classList.add("drag");
  })
);
["dragleave", "drop"].forEach((ev) =>
  els.drop.addEventListener(ev, (e) => {
    e.preventDefault();
    els.drop.classList.remove("drag");
  })
);
els.drop.addEventListener("drop", (e) => uploadFiles(e.dataTransfer.files));

loadBooks();
