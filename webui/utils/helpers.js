export function escapeHtml(str) {
    return String(str || "").replace(
        /[&<>'"]/g,
        (c) =>
            ({
                "&": "&amp;",
                "<": "&lt;",
                ">": "&gt;",
                "'": "&#39;",
                '"': "&quot;",
            })[c],
    );
}

export function fmtDuration(sec) {
    if (!isFinite(sec) || sec <= 0) return "0m";
    const m = Math.floor(sec / 60);
    const h = Math.floor(m / 60);
    const mm = m % 60;
    return h > 0 ? `${h}h ${mm}m` : `${mm}m`;
}

export function normalizeTimestamp(ts) {
    const num = Number(ts || 0);
    if (!num) return 0;
    return num > 1e12 ? Math.round(num / 1000) : num;
}

export function formatDayLabel(date) {
    const today = new Date();
    const startToday = new Date(today.getFullYear(), today.getMonth(), today.getDate());
    const startDate = new Date(date.getFullYear(), date.getMonth(), date.getDate());
    const diffDays = Math.round((startToday - startDate) / (24 * 3600 * 1000));
    if (diffDays === 0) return `Today, ${date.toLocaleDateString(undefined, { month: "short", day: "numeric" })}`;
    if (diffDays === 1)
        return `Yesterday, ${date.toLocaleDateString(undefined, { month: "short", day: "numeric" })}`;
    return date.toLocaleDateString(undefined, { weekday: "long", month: "short", day: "numeric" });
}

export function formatTimeRange(start, end) {
    if (!start || !end) return "";
    const opts = { hour: "numeric", minute: "2-digit" };
    return `${start.toLocaleTimeString(undefined, opts)} — ${end.toLocaleTimeString(undefined, opts)}`;
}

export function formatTime(seconds) {
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = Math.floor(seconds % 60);
    if (hours > 0) {
        return `${hours}h ${minutes}m`;
    } else if (minutes > 0) {
        return `${minutes}m ${secs}s`;
    } else {
        return `${secs}s`;
    }
}

export function truncateText(value, maxLength) {
    const text = String(value || "");
    if (text.length <= maxLength) return text;
    return `${text.slice(0, Math.max(0, maxLength - 1))}…`;
}

export function hashStringToHue(value) {
    const str = String(value || "");
    let hash = 0;
    for (let i = 0; i < str.length; i += 1) {
        hash = (hash * 31 + str.charCodeAt(i)) % 360;
    }
    return hash;
}

export function appIdBadgeStyle(appId) {
    const hue = hashStringToHue(appId);
    return {
        background: `hsl(${hue} 70% 95%)`,
        color: `hsl(${hue} 45% 30%)`,
        borderColor: `hsl(${hue} 70% 85%)`,
    };
}

export function getCategoryColor(category) {
    const key = String(category || "").toLowerCase();
    if (this?.categoryColorMap && this.categoryColorMap[key]) return this.categoryColorMap[key];
    const hue = hashStringToHue(key || "uncategorized");
    return `hsl(${hue} 70% 45%)`;
}

export function getDailyColorClass(color) {
    const key = String(color || "")
        .trim()
        .toLowerCase();
    const anytypeMap = {
        "anytype-yellow": "anytype-tag anytype-tag-yellow",
        "anytype-orange": "anytype-tag anytype-tag-orange",
        "anytype-red": "anytype-tag anytype-tag-red",
        "anytype-pink": "anytype-tag anytype-tag-pink",
        "anytype-purple": "anytype-tag anytype-tag-purple",
        "anytype-blue": "anytype-tag anytype-tag-blue",
        "anytype-ice": "anytype-tag anytype-tag-ice",
        "anytype-teal": "anytype-tag anytype-tag-teal",
        "anytype-lime": "anytype-tag anytype-tag-lime",
    };

    if (key && anytypeMap[key]) return anytypeMap[key];
    if (!key) {
        return "bg-slate-50 text-slate-600 dark:bg-slate-500/10 dark:text-slate-400";
    }
    return `bg-${key}-50 text-${key}-600 dark:bg-${key}-500/10 dark:text-${key}-400`;
}

// Token-input helpers (only used if those inputs exist in the DOM)
export function ensureCursor(el) {
    if (!el) return;
    let cursor = el.querySelector("[data-cursor]");
    if (cursor) return;

    cursor = document.createElement("span");
    cursor.setAttribute("data-cursor", "true");
    cursor.contentEditable = "true";
    cursor.spellcheck = false;
    cursor.className = "inline-block min-w-[0.5ch] outline-none";
    cursor.innerText = "\u00A0";

    el.appendChild(cursor);
}

export function placeCaretAtCursor(cursorEl) {
    try {
        if (!cursorEl) return;
        const range = document.createRange();
        range.selectNodeContents(cursorEl);
        range.collapse(false);
        const sel = window.getSelection();
        if (!sel) return;
        sel.removeAllRanges();
        sel.addRange(range);
    } catch {
        // ignore
    }
}

export function setupTokenInput(el) {
    if (!el) return;
    this.ensureCursor(el);

    el.addEventListener("focus", () => {
        this.ensureCursor(el);
        const cursor = el.querySelector("[data-cursor]");
        if (cursor) setTimeout(() => this.placeCaretAtCursor(cursor), 0);
    });

    el.addEventListener("click", (ev) => {
        const target = ev.target;
        if (
            target &&
            target.nodeType === 1 &&
            !target.hasAttribute("data-cursor") &&
            target.tagName.toLowerCase() === "span"
        ) {
            const cursor = el.querySelector("[data-cursor]");
            if (cursor) {
                ev.preventDefault();
                this.placeCaretAtCursor(cursor);
            }
        }
    });

    el.addEventListener("keydown", (e) => {
        if (e.key === ";") {
            e.preventDefault();

            const cursor = el.querySelector("[data-cursor]");
            const value = cursor ? cursor.innerText.trim() : "";
            if (!value) return;

            const tag = document.createElement("span");
            tag.textContent = value;
            tag.contentEditable = "false";
            tag.className =
                "mr-1 p-2 mb-1 px-2 py-0.5 rounded bg-primary/10 text-primary text-xs font-mono inline-block";

            el.insertBefore(tag, cursor);
            if (cursor) cursor.innerText = "\u00A0";

            if (cursor) this.placeCaretAtCursor(cursor);
        }
    });
}
