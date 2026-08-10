// ===========================================================================
//  E-Reader Web Portal — Interaction & Feedback
// ===========================================================================
//  Enhanced UX: toast notifications, delete confirmation modal, skeleton
//  loading, optimistic updates, file validation, and ARIA progress updates.
// ===========================================================================
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
  toasts: $("#toasts"),
  deleteModal: $("#deleteModal"),
  modalBody: $("#modal-body"),
  modalCancel: $("#modalCancel"),
  modalConfirm: $("#modalConfirm"),
};

// --- Utilities -------------------------------------------------------------

function fmtSize(bytes) {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / 1024 / 1024).toFixed(1) + " MB";
}

function escapeHtml(s) {
  return s.replace(/[&<>"']/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

// --- Toast Notifications ---------------------------------------------------

function showToast(message, type = "info", duration = 3000) {
  const toast = document.createElement("div");
  toast.className = `toast toast-${type}`;
  toast.innerHTML = `
    <span>${escapeHtml(message)}</span>
    <button class="toast-dismiss" aria-label="Dismiss">&times;</button>
  `;

  const dismiss = () => {
    toast.style.animation = "fadeIn 150ms ease reverse";
    setTimeout(() => toast.remove(), 150);
  };

  toast.querySelector(".toast-dismiss").onclick = dismiss;
  els.toasts.appendChild(toast);

  if (duration > 0) {
    setTimeout(dismiss, duration);
  }

  return toast;
}

// --- Delete Confirmation Modal ---------------------------------------------

let pendingDeletePath = null;
let pendingDeleteName = null;

function openDeleteModal(path, name) {
  pendingDeletePath = path;
  pendingDeleteName = name;
  els.modalBody.textContent = `Are you sure you want to delete "${name}"? This action cannot be undone.`;
  els.deleteModal.hidden = false;
  els.modalCancel.focus();
}

function closeDeleteModal() {
  els.deleteModal.hidden = true;
  pendingDeletePath = null;
  pendingDeleteName = null;
}

els.modalCancel.onclick = closeDeleteModal;

els.modalConfirm.onclick = async () => {
  if (!pendingDeletePath) return;
  const path = pendingDeletePath;
  const name = pendingDeleteName;
  closeDeleteModal();

  // Optimistic update: remove from UI immediately
  const removedItem = els.books.querySelector(`[data-path="${CSS.escape(path)}"]`);
  if (removedItem) {
    removedItem.style.animation = "fadeIn 150ms ease reverse";
    setTimeout(() => removedItem.remove(), 150);
  }

  try {
    const res = await fetch("/api/delete?path=" + encodeURIComponent(path), { method: "POST" });
    if (!res.ok) throw new Error("Delete failed");
    showToast(`"${name}" deleted`, "success");
    loadBooks(); // Refresh to confirm
  } catch (e) {
    showToast(`Failed to delete "${name}"`, "error", 5000);
    loadBooks(); // Rollback
  }
};

// Close modal on backdrop click
els.deleteModal.querySelector(".modal-backdrop").onclick = closeDeleteModal;

// Close modal on Escape
document.addEventListener("keydown", (e) => {
  if (e.key === "Escape" && !els.deleteModal.hidden) {
    closeDeleteModal();
  }
});

// --- Skeleton Loading ------------------------------------------------------

function showSkeletons(count = 3) {
  els.books.innerHTML = "";
  for (let i = 0; i < count; i++) {
    const li = document.createElement("li");
    li.innerHTML = `
      <div style="flex: 1;">
        <div class="skeleton skeleton-text" style="width: 70%;"></div>
        <div class="skeleton skeleton-text" style="width: 30%;"></div>
      </div>
    `;
    els.books.appendChild(li);
  }
}

// --- Book List -------------------------------------------------------------

async function loadBooks() {
  showSkeletons(); // Show loading state immediately

  try {
    const res = await fetch("/api/books");
    const data = await res.json();
    const type = data.storage === "sd" ? "microSD" : "flash";
    const free = data.space && data.space.free != null ? data.space.free : null;
    els.storage.textContent =
      free != null ? fmtSize(free) + " free · " + type : "storage: " + type;
    renderBooks(data.books || []);
  } catch (e) {
    els.books.innerHTML = `
      <li class="empty" role="listitem">
        <div class="empty-state">
          <div class="empty-icon" aria-hidden="true">
            <svg width="64" height="64" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round">
              <circle cx="12" cy="12" r="10"/>
              <line x1="12" y1="8" x2="12" y2="12"/>
              <line x1="12" y1="16" x2="12.01" y2="16"/>
            </svg>
          </div>
          <p>Could not reach the device.</p>
        </div>
      </li>
    `;
    showToast("Connection failed", "error", 5000);
  }
}

function renderBooks(books) {
  els.count.textContent = books.length ? "(" + books.length + ")" : "";

  if (!books.length) {
    els.books.innerHTML = `
      <li class="empty" role="listitem">
        <div class="empty-state">
          <div class="empty-icon" aria-hidden="true">
            <svg width="64" height="64" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" stroke-linejoin="round">
              <path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/>
              <path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/>
            </svg>
          </div>
          <p>Your library is empty — upload your first book!</p>
        </div>
      </li>
    `;
    return;
  }

  els.books.innerHTML = "";
  books.forEach((b) => {
    const li = document.createElement("li");
    li.setAttribute("role", "listitem");
    li.setAttribute("data-path", b.path);
    li.style.animation = "fadeIn 200ms ease";

    const info = document.createElement("div");
    info.innerHTML =
      `<div class="book-name">${escapeHtml(b.name)}</div>` +
      `<div class="book-meta">${fmtSize(b.size)}</div>`;

    const del = document.createElement("button");
    del.className = "del";
    del.innerHTML = `
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
        <polyline points="3 6 5 6 21 6"/>
        <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>
      </svg>
    `;
    del.setAttribute("aria-label", `Delete ${b.name}`);
    del.onclick = () => openDeleteModal(b.path, b.name);

    li.append(info, del);
    els.books.appendChild(li);
  });
}

// --- File Validation -------------------------------------------------------

function validateFile(file) {
  const isTxt = file.name.toLowerCase().endsWith(".txt") || file.type === "text/plain";
  if (!isTxt) {
    showToast(`"${file.name}" is not a .txt file`, "error", 5000);
    return false;
  }

  const MAX_SIZE = 5 * 1024 * 1024; // 5MB warning threshold
  if (file.size > MAX_SIZE) {
    showToast(`"${file.name}" is large (${fmtSize(file.size)}) — may take time`, "info", 5000);
  }

  return true;
}

// --- Uploads (one at a time, with a live progress bar) --------------------

function uploadFiles(fileList) {
  const files = Array.from(fileList).filter(validateFile);
  if (!files.length) return;

  let i = 0;
  const total = files.length;

  const next = () => {
    if (i >= files.length) {
      els.status.textContent = `Done — ${total} book${total > 1 ? "s" : ""} uploaded`;
      els.progressWrap.hidden = true;
      els.progressWrap.setAttribute("aria-valuenow", "0");
      showToast(`${total} book${total > 1 ? "s" : ""} uploaded`, "success");
      loadBooks();
      return;
    }
    const file = files[i++];
    const form = new FormData();
    form.append("file", file, file.name);

    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/upload");
    els.progressWrap.hidden = false;
    els.status.textContent = `Uploading ${file.name}… (${i}/${total})`;

    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) {
        const pct = (e.loaded / e.total) * 100;
        els.progressBar.style.width = pct + "%";
        els.progressWrap.setAttribute("aria-valuenow", Math.round(pct));
      }
    };
    xhr.onload = () => {
      els.progressBar.style.width = "0%";
      next();
    };
    xhr.onerror = () => {
      els.status.textContent = "Upload failed for " + file.name;
      showToast(`Failed to upload "${file.name}"`, "error", 5000);
    };
    xhr.send(form);
  };
  next();
}

// --- Wire up drag & drop + file picker ------------------------------------

els.pick.onclick = () => els.file.click();
els.file.onchange = () => uploadFiles(els.file.files);

// Dropzone keyboard accessibility
els.drop.onkeydown = (e) => {
  if (e.key === "Enter" || e.key === " ") {
    e.preventDefault();
    els.file.click();
  }
};

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

els.drop.addEventListener("drop", (e) => {
  uploadFiles(e.dataTransfer.files);
});

// --- Initialize ------------------------------------------------------------

loadBooks();
