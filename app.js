class FocusApp {
    constructor() {
        // State variables
        this.lastCategories = [];
        this.allowedApps = [];
        this.allowedTitles = [];
        this.lastTasks = [];
        this.lastAnytypeSpaces = [];
        this.anytypeChallengeId = "";
        this.currentTaskId = localStorage.getItem("currentTaskId") || null;
        this.lastFocusWarningKey = "";
        this.lastCurrentFocus = null;
        this.currentDurationValue = "25m";
        this.timerRunning = false;
        this.timerSeconds = 0;
        this.timerInterval = null;
        this.audioContext = null;
        this.currentView = "tasks";
        this.lastPageActive = this.isPageActive();
        this.anytypeError = null;
        this.amIFocused = false;
        this.monitoringEnabled = true;

        this.pomodoro = {
            isRunning: false,
            isPaused: false,
            mode: "focus", // 'focus', 'short-break', 'long-break'
            focusDuration: 25 * 60,
            shortBreakDuration: 5 * 60,
            longBreakDuration: 15 * 60,
            timeLeft: 25 * 60,
            interval: null,
            autoStartBreaks: true,
        };
        this.initPomodoro();

        // Set default filter mode
        window.historyFilterMode = "month";

        // Initialize methods that need to be bound
        this.refreshFocusOnly = this.refreshFocusOnly.bind(this);
        this.refreshAll = this.refreshAll.bind(this);
        this.setView = this.setView.bind(this);
        this.handleKeyDown = this.handleKeyDown.bind(this);
        // this.handlePageVisibility = this.handlePageVisibility.bind(this);
        // this.handlePageFocus = this.handlePageFocus.bind(this);
        this.handleFocus = this.handleFocus.bind(this);
        this.handleBlur = this.handleBlur.bind(this);

        // Bind methods used as event handlers
        this.submitTask = this.submitTask.bind(this);
        this.startTimer = this.startTimer.bind(this);
        this.stopTimer = this.stopTimer.bind(this);
        this.openDurationModal = this.openDurationModal.bind(this);
        this.closeDurationModal = this.closeDurationModal.bind(this);
        this.saveDurationModal = this.saveDurationModal.bind(this);
        this.openAnytypeConfigModal = this.openAnytypeConfigModal.bind(this);
        this.closeAnytypeConfigModal = this.closeAnytypeConfigModal.bind(this);
        this.createAnytypeChallenge = this.createAnytypeChallenge.bind(this);
        this.createAnytypeApiKey = this.createAnytypeApiKey.bind(this);
        this.loadAnytypeSpaces = this.loadAnytypeSpaces.bind(this);
        this.saveAnytypeSpace = this.saveAnytypeSpace.bind(this);
        this.saveAnytypeConfig = this.saveAnytypeConfig.bind(this);
        this.refreshAnytypeCache = this.refreshAnytypeCache.bind(this);
        // this.openMeetingsEditor = this.openMeetingsEditor.bind(this);

        // Meeting app token storage (persisted)
        this.meetingAppIds = JSON.parse(localStorage.getItem("meetingAppIds") || "[]");
        this.meetingAppTitles = JSON.parse(localStorage.getItem("meetingAppTitles") || "[]");
        // Email app token storage (persisted)
        this.emailAppIds = JSON.parse(localStorage.getItem("emailAppIds") || "[]");
        this.emailAppTitles = JSON.parse(localStorage.getItem("emailAppTitles") || "[]");
        // Notes app token storage (persisted)
        this.noteAppIds = JSON.parse(localStorage.getItem("noteAppIds") || "[]");
        this.noteAppTitles = JSON.parse(localStorage.getItem("noteAppTitles") || "[]");
        // Excluded tokens (will be hidden from modals)
        this.excludedAppIds = JSON.parse(localStorage.getItem("excludedAppIds") || "[]");
        this.excludedAppTitles = JSON.parse(localStorage.getItem("excludedAppTitles") || "[]");
    }

    // ==================== MEETING APP TOKEN INPUTS & MODAL ====================
    openMeetingsEditor(event) {
        event.stopPropagation();
        document.getElementById("category-editor").classList.remove("hidden");
    }

    openAddMeetingAppModal() {
        console.log("openAddMeetingAppModal()");
        const modal = document.getElementById("addMeetingAppModal");
        if (modal) modal.classList.remove("hidden");
        // populate token fields from stored arrays
        const idsEl = document.getElementById("meetingAppIds");
        const titlesEl = document.getElementById("meetingAppTitles");
        if (idsEl)
            this._renderTokenField(
                idsEl,
                (Array.isArray(this.meetingAppIds) ? this.meetingAppIds : []).filter(
                    (v) =>
                        !this.excludedAppIds
                            .map((x) => String(x || "").toLowerCase())
                            .includes(String(v || "").toLowerCase()),
                ),
            );
        if (titlesEl)
            this._renderTokenField(
                titlesEl,
                (Array.isArray(this.meetingAppTitles) ? this.meetingAppTitles : []).filter(
                    (v) =>
                        !this.excludedAppTitles
                            .map((x) => String(x || "").toLowerCase())
                            .includes(String(v || "").toLowerCase()),
                ),
            );

        const el = document.getElementById("meetingAppIds");
        if (el) {
            this.ensureCursor(el);
            el.focus();
            setTimeout(() => {
                const cursor = el.querySelector("[data-cursor]");
                if (cursor) this.placeCaretAtCursor(cursor);
            }, 0);
        }
    }

    // ==================== EMAIL APP TOKEN INPUTS & MODAL ====================
    openAddEmailAppModal() {
        console.log("openAddEmailAppModal()");
        const modal = document.getElementById("addEmailAppModal");
        if (modal) modal.classList.remove("hidden");
        const idsEl = document.getElementById("emailAppIds");
        const titlesEl = document.getElementById("emailAppTitles");
        if (idsEl)
            this._renderTokenField(
                idsEl,
                (Array.isArray(this.emailAppIds) ? this.emailAppIds : []).filter(
                    (v) =>
                        !this.excludedAppIds
                            .map((x) => String(x || "").toLowerCase())
                            .includes(String(v || "").toLowerCase()),
                ),
            );
        if (titlesEl)
            this._renderTokenField(
                titlesEl,
                (Array.isArray(this.emailAppTitles) ? this.emailAppTitles : []).filter(
                    (v) =>
                        !this.excludedAppTitles
                            .map((x) => String(x || "").toLowerCase())
                            .includes(String(v || "").toLowerCase()),
                ),
            );

        const el = document.getElementById("emailAppIds");
        if (el) {
            this.ensureCursor(el);
            el.focus();
            setTimeout(() => {
                const cursor = el.querySelector("[data-cursor]");
                if (cursor) this.placeCaretAtCursor(cursor);
            }, 0);
        }
    }

    closeAddEmailAppModal() {
        const modal = document.getElementById("addEmailAppModal");
        if (modal) modal.classList.add("hidden");
    }

    // ==================== NOTE APP TOKEN INPUTS & MODAL ====================
    openAddNoteAppModal() {
        console.log("openAddNoteAppModal()");
        const modal = document.getElementById("addNoteAppModal");
        if (modal) modal.classList.remove("hidden");
        const idsEl = document.getElementById("noteAppIds");
        const titlesEl = document.getElementById("noteAppTitles");
        if (idsEl)
            this._renderTokenField(
                idsEl,
                (Array.isArray(this.noteAppIds) ? this.noteAppIds : []).filter(
                    (v) =>
                        !this.excludedAppIds
                            .map((x) => String(x || "").toLowerCase())
                            .includes(String(v || "").toLowerCase()),
                ),
            );
        if (titlesEl)
            this._renderTokenField(
                titlesEl,
                (Array.isArray(this.noteAppTitles) ? this.noteAppTitles : []).filter(
                    (v) =>
                        !this.excludedAppTitles
                            .map((x) => String(x || "").toLowerCase())
                            .includes(String(v || "").toLowerCase()),
                ),
            );

        const el = document.getElementById("noteAppIds");
        if (el) {
            this.ensureCursor(el);
            el.focus();
            setTimeout(() => {
                const cursor = el.querySelector("[data-cursor]");
                if (cursor) this.placeCaretAtCursor(cursor);
            }, 0);
        }
    }

    closeAddNoteAppModal() {
        const modal = document.getElementById("addNoteAppModal");
        if (modal) modal.classList.add("hidden");
    }

    saveNoteApp() {
        const idsEl = document.getElementById("noteAppIds");
        const titlesEl = document.getElementById("noteAppTitles");
        const ids = this.getTokensFromEl(idsEl);
        const titles = this.getTokensFromEl(titlesEl);

        if (!ids.length && !titles.length) return this.closeAddNoteAppModal();

        this.noteAppIds ??= [];
        this.noteAppTitles ??= [];

        const excludedIdsLowerN = (Array.isArray(this.excludedAppIds) ? this.excludedAppIds : []).map((x) =>
            String(x || "").toLowerCase(),
        );
        const excludedTitlesLowerN = (Array.isArray(this.excludedAppTitles) ? this.excludedAppTitles : []).map((x) =>
            String(x || "").toLowerCase(),
        );

        // Replace stored note arrays with modal contents (respect excluded lists)
        this.noteAppIds = (Array.isArray(ids) ? ids : [])
            .map((v) => String(v || "").trim())
            .filter(Boolean)
            .filter((v) => !excludedIdsLowerN.includes(String(v).toLowerCase()));

        this.noteAppTitles = (Array.isArray(titles) ? titles : [])
            .map((v) => String(v || "").trim())
            .filter(Boolean)
            .filter((v) => !excludedTitlesLowerN.includes(String(v).toLowerCase()));

        try {
            localStorage.setItem("noteAppIds", JSON.stringify(this.noteAppIds));
            localStorage.setItem("noteAppTitles", JSON.stringify(this.noteAppTitles));
        } catch (e) {
            console.error("Failed to save note apps to localStorage", e);
        }

        const idsRenderEl = document.getElementById("noteAppIds");
        const titlesRenderEl = document.getElementById("noteAppTitles");
        if (idsRenderEl)
            this._renderTokenField(
                idsRenderEl,
                (Array.isArray(this.noteAppIds) ? this.noteAppIds : []).filter(
                    (v) => !excludedIdsLowerN.includes(String(v || "").toLowerCase()),
                ),
            );
        if (titlesRenderEl)
            this._renderTokenField(
                titlesRenderEl,
                (Array.isArray(this.noteAppTitles) ? this.noteAppTitles : []).filter(
                    (v) => !excludedTitlesLowerN.includes(String(v || "").toLowerCase()),
                ),
            );

        this.closeAddNoteAppModal();
    }

    saveEmailApp() {
        const idsEl = document.getElementById("emailAppIds");
        const titlesEl = document.getElementById("emailAppTitles");
        const ids = this.getTokensFromEl(idsEl);
        const titles = this.getTokensFromEl(titlesEl);

        if (!ids.length && !titles.length) return;

        const excludedIdsLower = (Array.isArray(this.excludedAppIds) ? this.excludedAppIds : []).map((x) =>
            String(x || "").toLowerCase(),
        );
        const excludedTitlesLower = (Array.isArray(this.excludedAppTitles) ? this.excludedAppTitles : []).map((x) =>
            String(x || "").toLowerCase(),
        );

        // Replace stored email arrays with modal contents (respect excluded lists)
        this.emailAppIds = (Array.isArray(ids) ? ids : [])
            .map((v) => String(v || "").trim())
            .filter(Boolean)
            .filter((v) => !excludedIdsLower.includes(String(v).toLowerCase()));

        this.emailAppTitles = (Array.isArray(titles) ? titles : [])
            .map((v) => String(v || "").trim())
            .filter(Boolean)
            .filter((v) => !excludedTitlesLower.includes(String(v).toLowerCase()));

        try {
            localStorage.setItem("emailAppIds", JSON.stringify(this.emailAppIds));
            localStorage.setItem("emailAppTitles", JSON.stringify(this.emailAppTitles));
        } catch (e) {
            console.warn("Failed to persist email apps to localStorage", e);
        }

        console.log("Saved email apps:", { ids: this.emailAppIds, titles: this.emailAppTitles });

        if (idsEl)
            this._renderTokenField(
                idsEl,
                (Array.isArray(this.emailAppIds) ? this.emailAppIds : []).filter(
                    (v) => !excludedIdsLower.includes(String(v || "").toLowerCase()),
                ),
            );
        if (titlesEl)
            this._renderTokenField(
                titlesEl,
                (Array.isArray(this.emailAppTitles) ? this.emailAppTitles : []).filter(
                    (v) => !excludedTitlesLower.includes(String(v || "").toLowerCase()),
                ),
            );

        this.closeAddEmailAppModal();
    }

    closeAddMeetingAppModal() {
        const modal = document.getElementById("addMeetingAppModal");
        if (modal) modal.classList.add("hidden");
    }

    async saveMeetingApp() {
        // Ensure arrays are initialized
        this.meetingAppIds ??= [];
        this.meetingAppTitles ??= [];

        const idsEl = document.getElementById("meetingAppIds");
        const titlesEl = document.getElementById("meetingAppTitles");

        const ids = this.getTokensFromEl(idsEl);
        const titles = this.getTokensFromEl(titlesEl);

        // Replace stored arrays with the content of the modal (respect excluded lists)
        const excludedIdsLowerM = (Array.isArray(this.excludedAppIds) ? this.excludedAppIds : []).map((x) =>
            String(x || "").toLowerCase(),
        );
        const excludedTitlesLowerM = (Array.isArray(this.excludedAppTitles) ? this.excludedAppTitles : []).map((x) =>
            String(x || "").toLowerCase(),
        );

        this.meetingAppIds = (Array.isArray(ids) ? ids : [])
            .map((v) => String(v || "").trim())
            .filter(Boolean)
            .filter((v) => !excludedIdsLowerM.includes(String(v).toLowerCase()));

        this.meetingAppTitles = (Array.isArray(titles) ? titles : [])
            .map((v) => String(v || "").trim())
            .filter(Boolean)
            .filter((v) => !excludedTitlesLowerM.includes(String(v).toLowerCase()));

        // Persist locally
        try {
            localStorage.setItem("meetingAppIds", JSON.stringify(this.meetingAppIds));
            localStorage.setItem("meetingAppTitles", JSON.stringify(this.meetingAppTitles));
        } catch (e) {
            console.warn("Failed to persist meeting apps to localStorage", e);
        }

        const payload = {
            appids: this.meetingAppIds,
            apptitles: this.meetingAppTitles,
            taskname: "Meet",
        };

        const res = await fetch("/api/v1/task/recurring_tasks", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload),
        });

        console.log(res);
        if (!res.ok) {
            console.warn("Failed to save recurring tasks", await res.text());
            return;
        }

        // Re-render fields so tokens are visible and cursor is correct
        if (idsEl)
            this._renderTokenField(
                idsEl,
                (Array.isArray(this.meetingAppIds) ? this.meetingAppIds : []).filter(
                    (v) => !excludedIdsLowerM.includes(String(v || "").toLowerCase()),
                ),
            );
        if (titlesEl)
            this._renderTokenField(
                titlesEl,
                (Array.isArray(this.meetingAppTitles) ? this.meetingAppTitles : []).filter(
                    (v) => !excludedTitlesLowerM.includes(String(v || "").toLowerCase()),
                ),
            );

        this.closeAddMeetingAppModal();
    }

    setupTokenInput(el) {
        if (!el) return;
        this.ensureCursor(el);

        // Place caret at cursor when focusing
        el.addEventListener("focus", () => {
            this.ensureCursor(el);
            const cursor = el.querySelector("[data-cursor]");
            if (cursor) setTimeout(() => this.placeCaretAtCursor(cursor), 0);
        });

        // Redirect clicks inside tokens to the cursor position
        el.addEventListener("click", (ev) => {
            const target = ev.target;
            // if clicked inside a token (span without data-cursor), move caret to cursor
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

    ensureCursor(el) {
        if (!el) return;
        let cursor = el.querySelector("[data-cursor]");
        if (!cursor) {
            cursor = document.createElement("span");
            cursor.dataset.cursor = "true";
            cursor.className = "outline-none min-w-[2px] inline-block";
            cursor.contentEditable = "true";
            cursor.innerHTML = "\u00A0";
            el.appendChild(cursor);
        } else {
            if (!cursor.innerHTML || cursor.innerHTML.trim() === "") cursor.innerHTML = "\u00A0";
            cursor.contentEditable = "true";
        }
        // ensure cursor is last child so typing appends after tokens
        if (el.lastChild !== cursor) el.appendChild(cursor);
        return cursor;
    }

    placeCaretAtCursor(cursor) {
        const range = document.createRange();
        range.selectNodeContents(cursor);
        range.collapse(false);

        const sel = window.getSelection();
        sel.removeAllRanges();
        sel.addRange(range);
    }

    readTokens(el) {
        if (!el) return [];
        return [...el.querySelectorAll("span:not([data-cursor])")].map((s) => {
            // prefer the first text node (token value) so we don't include the exclude button text
            if (s.childNodes && s.childNodes.length) {
                const first = s.childNodes[0];
                if (first && first.nodeType === Node.TEXT_NODE) return String(first.textContent || "").trim();
            }
            return String(s.textContent || "").trim();
        });
    }

    _createTokenSpan(value) {
        const tag = document.createElement("span");
        tag.textContent = value;
        tag.contentEditable = "false";
        tag.className = "mr-1 mb-1 px-2 py-0.5 rounded bg-primary/10 text-primary text-xs font-mono inline-block";
        return tag;
    }

    _renderTokenField(el, tokens) {
        if (!el) return;
        el.innerHTML = "";
        (Array.isArray(tokens) ? tokens : []).forEach((t) => {
            const span = this._createTokenSpan(t);
            // store metadata so exclude handler knows which list this came from
            span.dataset.tokenValue = String(t || "");
            span.dataset.parentId = el.id || "";

            // create exclude button (small ×) to allow hiding this token permanently
            const btn = document.createElement("button");
            btn.type = "button";
            btn.className = "ml-1 text-[10px] text-red-600 hover:text-red-800 inline-block align-middle";
            btn.setAttribute("aria-label", "exclude token");
            btn.textContent = "✖";
            btn.addEventListener("click", (e) => {
                e.preventDefault();
                e.stopPropagation();
                const val = span.dataset.tokenValue || "";
                const parent = span.dataset.parentId || "";
                if (!val) return;

                // decide whether this is an id field or title field
                const isTitle = /title/i.test(parent) || /titles/i.test(parent);

                // add to excluded lists
                try {
                    if (isTitle) {
                        this.excludedAppTitles ??= [];
                        if (
                            !this.excludedAppTitles.some(
                                (x) => String(x || "").toLowerCase() === String(val).toLowerCase(),
                            )
                        ) {
                            this.excludedAppTitles.push(val);
                            localStorage.setItem("excludedAppTitles", JSON.stringify(this.excludedAppTitles));
                        }
                    } else {
                        this.excludedAppIds ??= [];
                        if (
                            !this.excludedAppIds.some(
                                (x) => String(x || "").toLowerCase() === String(val).toLowerCase(),
                            )
                        ) {
                            this.excludedAppIds.push(val);
                            localStorage.setItem("excludedAppIds", JSON.stringify(this.excludedAppIds));
                        }
                    }
                } catch (err) {
                    console.error("Failed to persist exclusion", err);
                }

                // also remove from the underlying stored arrays for this modal (if present)
                try {
                    const mapping = {
                        meetingAppIds: "meetingAppIds",
                        meetingAppTitles: "meetingAppTitles",
                        emailAppIds: "emailAppIds",
                        emailAppTitles: "emailAppTitles",
                        noteAppIds: "noteAppIds",
                        noteAppTitles: "noteAppTitles",
                    };
                    const key = mapping[parent];
                    if (key && Array.isArray(this[key])) {
                        this[key] = this[key].filter(
                            (x) => String(x || "").toLowerCase() !== String(val).toLowerCase(),
                        );
                        try {
                            localStorage.setItem(key, JSON.stringify(this[key]));
                        } catch (err) {
                            console.warn("Failed to update stored tokens after exclusion", err);
                        }
                    }
                } catch (err) {
                    console.warn("Error removing token from stored arrays", err);
                }

                // remove span from DOM
                span.remove();
            });

            span.appendChild(btn);
            el.appendChild(span);
        });
        // ensure cursor at end
        this.ensureCursor(el);
    }

    attachCategoryEditors() {
        const rules = {
            meetings: [],
            email: [],
        };

        let activeCategory = null;
        const editor = document.getElementById("category-editor");
        const titleEl = document.getElementById("editor-title");
        const appIdInput = document.getElementById("editor-appid");
        const titleInput = document.getElementById("editor-title-input");
        const rulesList = document.getElementById("editor-rules");

        function open(category) {
            activeCategory = category;
            titleEl.textContent = `Rules for ${category}`;
            editor.classList.remove("hidden");
            render();
        }

        function close() {
            editor.classList.add("hidden");
            activeCategory = null;
        }

        function render() {
            rulesList.innerHTML = "";
            rules[activeCategory].forEach((r) => {
                const li = document.createElement("li");
                li.textContent =
                    `${r.app_id ? `app_id: ${r.app_id}` : ""}` +
                    `${r.app_id && r.title ? " | " : ""}` +
                    `${r.title ? `title: ${r.title}` : ""}`;
                rulesList.appendChild(li);
            });
        }

        document.querySelector('[data-action="edit-meet-apps"]')?.addEventListener("click", () => open("meetings"));
        document.querySelector('[data-action="edit-email-apps"]')?.addEventListener("click", () => open("email"));
        document.getElementById("editor-close").onclick = close;
        document.getElementById("editor-add").onclick = () => {
            if (!activeCategory) return;

            const app_id = appIdInput.value.trim();
            const title = titleInput.value.trim();
            if (!app_id && !title) return;

            rules[activeCategory].push({
                app_id: app_id || null,
                title: title || null,
            });

            appIdInput.value = "";
            titleInput.value = "";
            render();
        };
    }

    // ==================== UTILITY METHODS ====================
    escapeHtml(str) {
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

    fmtDuration(sec) {
        if (!isFinite(sec) || sec <= 0) return "0m";
        const m = Math.floor(sec / 60);
        const h = Math.floor(m / 60);
        const mm = m % 60;
        return h > 0 ? `${h}h ${mm}m` : `${mm}m`;
    }

    normalizeListInput(value) {
        return String(value || "")
            .split(/[\n,;]+/)
            .map((s) => s.replace(/\u00A0/g, " ").trim())
            .filter(Boolean);
    }

    // Robust extraction of tokens from a contenteditable token container.
    // First try structured tokens (span elements). If none, fallback to splitting visible text.
    getTokensFromEl(el) {
        if (!el) return [];
        try {
            const tokens = this.readTokens(el)
                .map((s) => String(s || "").trim())
                .filter(Boolean);
            if (tokens.length) return tokens;
            const txt = (el.innerText || el.textContent || "").replace(/\u00A0/g, " ");
            return this.normalizeListInput(txt);
        } catch (err) {
            try {
                const txt = (el.innerText || el.textContent || "").replace(/\u00A0/g, " ");
                return this.normalizeListInput(txt);
            } catch (e) {
                return [];
            }
        }
    }

    normalizeTimestamp(ts) {
        const num = Number(ts || 0);
        if (!num) return 0;
        return num > 1e12 ? Math.round(num / 1000) : num;
    }

    toLocalDateKey(date) {
        const y = date.getFullYear();
        const m = String(date.getMonth() + 1).padStart(2, "0");
        const d = String(date.getDate()).padStart(2, "0");
        return `${y}-${m}-${d}`;
    }

    formatDayLabel(date) {
        const today = new Date();
        const startToday = new Date(today.getFullYear(), today.getMonth(), today.getDate());
        const startDate = new Date(date.getFullYear(), date.getMonth(), date.getDate());
        const diffDays = Math.round((startToday - startDate) / (24 * 3600 * 1000));
        if (diffDays === 0) return `Today, ${date.toLocaleDateString(undefined, { month: "short", day: "numeric" })}`;
        if (diffDays === 1)
            return `Yesterday, ${date.toLocaleDateString(undefined, { month: "short", day: "numeric" })}`;
        return date.toLocaleDateString(undefined, { weekday: "long", month: "short", day: "numeric" });
    }

    formatTimeRange(start, end) {
        if (!start || !end) return "";
        const opts = { hour: "numeric", minute: "2-digit" };
        return `${start.toLocaleTimeString(undefined, opts)} — ${end.toLocaleTimeString(undefined, opts)}`;
    }

    formatTime(seconds) {
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

    truncateText(value, maxLength) {
        const text = String(value || "");
        if (text.length <= maxLength) return text;
        return `${text.slice(0, Math.max(0, maxLength - 1))}…`;
    }

    hashStringToHue(value) {
        const str = String(value || "");
        let hash = 0;
        for (let i = 0; i < str.length; i += 1) {
            hash = (hash * 31 + str.charCodeAt(i)) % 360;
        }
        return hash;
    }

    appIdBadgeStyle(appId) {
        const hue = this.hashStringToHue(appId);
        return {
            background: `hsl(${hue} 70% 95%)`,
            color: `hsl(${hue} 45% 30%)`,
            borderColor: `hsl(${hue} 70% 85%)`,
        };
    }

    badgeClassForCategory(category) {
        const palette = [
            "bg-purple-100 text-purple-700 border-purple-200",
            "bg-orange-100 text-orange-700 border-orange-200",
            "bg-emerald-100 text-emerald-700 border-emerald-200",
            "bg-blue-100 text-blue-700 border-blue-200",
            "bg-slate-100 text-slate-700 border-slate-200",
        ];
        const key = String(category || "").toLowerCase();
        if (!key) return palette[4];
        let hash = 0;
        for (let i = 0; i < key.length; i += 1) {
            hash = (hash * 31 + key.charCodeAt(i)) % palette.length;
        }
        return palette[hash];
    }

    //╭─────────────────────────────────────╮
    //│             API METHODS             │
    //╰─────────────────────────────────────╯
    async loadTasks() {
        this.anytypeError = null;
        try {
            const res = await fetch("/api/v1/anytype/tasks", { cache: "no-store" });
            if (!res.ok) {
                const text = await res.text();
                this.anytypeError = text || "Failed to load Anytype tasks.";
                return [];
            }
            const tasks = await res.json();
            return Array.isArray(tasks) ? tasks : [];
        } catch (err) {
            console.error("Failed to load Anytype tasks", err);
            this.anytypeError = "Failed to load Anytype tasks.";
            return [];
        }
    }

    async loadCurrent() {
        const res = await fetch("/api/v1/current", { cache: "no-store" });
        if (!res.ok) return null;
        const cur = await res.json();
        return Object.keys(cur || {}).length ? cur : null;
    }

    async loadMonitoringState() {
        try {
            const res = await fetch("/api/v1/monitoring", { cache: "no-store" });
            if (res.ok) {
                const data = await res.json();
                this.monitoringEnabled = data.enabled;
                const toggle = document.getElementById("monitoring-toggle");
                if (toggle) {
                    toggle.checked = this.monitoringEnabled;
                }
            }
        } catch (err) {
            console.error("Failed to load monitoring state", err);
        }
    }

    async loadSettings() {
        try {
            const res = await fetch("/api/v1/settings", { cache: "no-store" });
            if (res.ok) {
                const data = await res.json();
                this.monitoringEnabled = data.monitoring_enabled;
                const toggle = document.getElementById("monitoring-toggle");
                if (toggle) {
                    toggle.checked = this.monitoringEnabled;
                }
                if (data.current_task_id) {
                    this.setCurrentTaskId(data.current_task_id);
                }
            }
        } catch (err) {
            console.error("Failed to load settings", err);
        }
    }

    async loadEvents() {
        const res = await fetch("/api/v1/events", { cache: "no-store" });
        if (!res.ok) return [];
        const events = await res.json();
        // console.log(events);
        if (!Array.isArray(events)) return [];
        return events;
    }

    async loadCategories() {
        const res = await fetch("/api/v1/categories", { cache: "no-store" });
        if (!res.ok) return [];
        const categories = await res.json();
        if (!Array.isArray(categories)) return [];
        return categories.filter((c) => c && c.category);
    }

    async loadHistory() {
        const res = await fetch("/api/v1/history", { cache: "no-store" });
        if (!res.ok) return [];
        const history = await res.json();
        if (!Array.isArray(history)) return [];
        return history;
    }

    async updateServerFocusRules(task) {
        try {
            const payload = task
                ? {
                      allowed_app_ids: Array.isArray(task.allowed_app_ids) ? task.allowed_app_ids : [],
                      allowed_titles: Array.isArray(task.allowed_titles) ? task.allowed_titles : [],
                      task_title: task.title || "",
                  }
                : { allowed_app_ids: [], allowed_titles: [], task_title: "" };
            await fetch("/api/v1/focus/rules", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(payload),
            });
        } catch (err) {
            console.warn("Failed to update focus rules", err);
        }
    }

    async updateTasksCategories() {
        const res = await fetch("/api/v1/anytype/tasks_categories");
        if (!res.ok) return;

        const days = 1;
        const res2 = await fetch(`/api/v1/focus/category-percentages?days=${days}`, {
            method: "GET",
            headers: { "Content-Type": "application/json" },
        });

        if (!res2.ok) return;
        const res2json = await res2.json();

        const body = await res.json();
        const categories = body.data;
        const legend = document.getElementById("tasks-categories");

        legend.innerHTML = categories
            .map((cat) => {
                // find percentage for this category
                const percEntry = res2json.find((p) => p.category === cat.name);
                const perc = percEntry ? percEntry.percentage : 0;

                return `
        <div class="flex items-center justify-between text-sm">
            <div class="flex items-center gap-2">
                <span
                    class="w-3 h-3 rounded-full"
                    style="background-color: var(--anytype-color-tag-${cat.color});"
                ></span>
                <span class="text-gray-600 dark:text-gray-400">${cat.name}</span>
            </div>
            <span class="font-medium text-gray-900 dark:text-white">
                ${Number(perc).toFixed(2)}%
            </span>
        </div>
        `;
            })
            .join("");
    }

    async updateDailyFocus() {
        // use selected history range when computing focus statistics
        const days = 1; //Number(this.historyDays) || 1;
        const res = await fetch(`/api/v1/focus/today?days=${days}`);
        if (!res.ok) {
            console.error("API '/focus/today' not returned ok");
            return;
        }

        const data = await res.json();
        const focusedSeconds = Number(data.focused_seconds ?? 0);
        const unfocusedSeconds = Number(data.unfocused_seconds ?? 0);
        const totalSeconds = focusedSeconds + unfocusedSeconds;
        // aggregate app totals across the selected days for the history pie/legend
        const aggregated = {};
        Object.keys(data || {}).forEach((d) => {
            const apps = data[d] || {};
            Object.entries(apps).forEach(([appId, titles]) => {
                let appTotal = 0;
                Object.values(titles || {}).forEach((sec) => (appTotal += Number(sec || 0)));
                aggregated[appId] = (aggregated[appId] || 0) + appTotal;
            });
        });

        // render history pie and legend
        try {
            const pieEl = document.getElementById("history-pie");
            const legendEl = document.getElementById("history-legend");
            const monthLabel = document.getElementById("history-month-label");
            const goalText = document.getElementById("history-goal-text");
            const goalBar = document.getElementById("history-goal-progress");

            const appEntries = Object.entries(aggregated).sort((a, b) => b[1] - a[1]);
            const total = appEntries.reduce((s, [, v]) => s + v, 0) || 0;

            // prepare slices (top 6 + others)
            const colors = ["#2563eb", "#a855f7", "#f97316", "#ef4444", "#10b981", "#06b6d4"];
            const slices = [];
            let others = 0;
            appEntries.forEach((entry, idx) => {
                if (idx < 6) slices.push({ app: entry[0], secs: entry[1], color: colors[idx % colors.length] });
                else others += entry[1];
            });
            if (others > 0) slices.push({ app: "Others", secs: others, color: "#94a3b8" });

            if (pieEl) {
                let offset = 0;
                const parts = slices.map((s) => {
                    const pct = total > 0 ? (s.secs / total) * 100 : 0;
                    const start = offset;
                    offset += pct;
                    const end = offset;
                    return `${s.color} ${start}% ${end}%`;
                });
                pieEl.style.background = `conic-gradient(${parts.join(", ")})`;
            }

            if (legendEl) {
                legendEl.innerHTML = "";
                slices.forEach((s) => {
                    const row = document.createElement("div");
                    row.className = "flex items-center justify-between text-sm";
                    const pct = total > 0 ? (s.secs / total) * 100 : 0;
                    row.innerHTML = `
                        <div class=\"flex items-center gap-2\"> 
                            <span class=\"w-3 h-3 rounded-full\" style=\"background:${s.color}\"></span>
                            <span class=\"text-gray-600 dark:text-gray-400\">${this.escapeHtml(s.app)}</span>
                        </div>
                        <div class=\"text-right\"> 
                            <div class=\"font-medium text-gray-900 dark:text-white\">${this.fmtDuration(s.secs)}</div>
                            <div class=\"text-xs text-gray-500\">${pct.toFixed(1)}%</div>
                        </div>
                    `;
                    legendEl.appendChild(row);
                });
            }

            if (monthLabel) {
                // show a label based on selected range
                const days = Number(this.historyDays) || 30;
                const now = new Date();
                if (days <= 7)
                    monthLabel.textContent = now.toLocaleDateString(undefined, { month: "short", day: "numeric" });
                else if (days <= 31)
                    monthLabel.textContent = now.toLocaleDateString(undefined, { month: "short", year: "numeric" });
                else monthLabel.textContent = now.toLocaleDateString(undefined, { month: "short", year: "numeric" });
            }

            if (goalText)
                goalText.textContent = `${this.fmtDuration(focusedSeconds)} over ${Object.keys(data).length} days`;
            if (goalBar) {
                // set a simple goal: 1 hour per day
                const days = Number(this.historyDays) || 30;
                const goalSeconds = days * 3600;
                const pct = goalSeconds > 0 ? Math.min(100, (focusedSeconds / goalSeconds) * 100) : 0;
                goalBar.style.width = `${pct}%`;
            }
        } catch (err) {
            console.warn("Failed to render history pie/legend", err);
        }

        // ─────────────────────────────────────
        // Elements
        const container = document.getElementById("i-was-focused");
        const bar = document.getElementById("focus-progress-bar");
        const text = container?.querySelector("p");
        const pie = document.getElementById("stats-pie");
        const legend = document.getElementById("stats-legend");
        const totalEl = document.getElementById("stats-total");
        const totalText = document.getElementById("stats-text");
        const loadingEl = document.getElementById("stats-pie-loading");

        if (!container || !bar || !text || !pie || !totalEl || !totalText) return;

        totalText.textContent = "Total Time";

        if (loadingEl) loadingEl.style.display = "block";

        // ─────────────────────────────────────
        // ZERO DATA STATE
        if (totalSeconds === 0) {
            // Bar
            bar.style.width = "0%";
            bar.className = bar.className.replace(/bg-\S+/, "").trim() + " bg-gray-300";

            // Container gradient (neutral)
            container.className =
                container.className.replace(/from-\S+ to-\S+/, "").trim() +
                " bg-gradient-to-br from-gray-200 to-gray-300";

            // Text
            text.textContent = "No focus data for today";

            // Pie (empty)
            pie.style.background = "#e5e7eb"; // gray-200

            // Total
            totalEl.textContent = this.fmtDuration(0);

            // Legend
            if (legend) {
                legend.innerHTML = `
                <div class="flex items-center justify-between text-sm">
                    <div class="flex items-center gap-2">
                        <span class="w-3 h-3 rounded-full bg-gray-400"></span>
                        <span class="text-gray-600 dark:text-gray-400">Focused</span>
                    </div>
                    <span class="font-medium text-gray-900 dark:text-white">0%</span>
                </div>
                <div class="flex items-center justify-between text-sm">
                    <div class="flex items-center gap-2">
                        <span class="w-3 h-3 rounded-full bg-gray-400"></span>
                        <span class="text-gray-600 dark:text-gray-400">Not Focused</span>
                    </div>
                    <span class="font-medium text-gray-900 dark:text-white">0%</span>
                </div>
            `;
            }

            if (loadingEl) loadingEl.style.display = "none";
            return;
        }

        // ─────────────────────────────────────
        // NORMAL STATE
        const focusPercent = Math.min(100, (focusedSeconds / totalSeconds) * 100);
        const unfocusPercent = Math.min(100, (unfocusedSeconds / totalSeconds) * 100);

        let gradient = "";
        let barColor = "";
        let message = "";

        if (focusPercent < 20) {
            gradient = "from-red-600 to-red-800";
            barColor = "bg-red-300";
            message = "You were not focused today";
        } else if (focusPercent < 40) {
            gradient = "from-orange-500 to-orange-700";
            barColor = "bg-orange-200";
            message = "You were less focused today";
        } else if (focusPercent < 60) {
            gradient = "from-yellow-400 to-yellow-600";
            barColor = "bg-yellow-100";
            message = "Your focus was inconsistent today";
        } else if (focusPercent < 80) {
            gradient = "from-green-400 to-green-600";
            barColor = "bg-green-100";
            message = "You were mostly focused today";
        } else {
            gradient = "from-emerald-500 to-emerald-700";
            barColor = "bg-emerald-100";
            message = "You were very focused today";
        }

        container.className =
            container.className.replace(/from-\S+ to-\S+/, "").trim() + ` bg-gradient-to-br ${gradient}`;

        bar.className = bar.className.replace(/bg-\S+/, "").trim() + ` ${barColor}`;
        bar.style.width = `${focusPercent.toFixed(1)}%`;

        text.textContent =
            `${message} (${focusPercent.toFixed(0)}% focused, ` +
            `${((unfocusedSeconds / totalSeconds) * 100).toFixed(0)}% not focused)`;

        // ─────────────────────────────────────
        // Pie + legend
        const defaultMode = this.statsLegendMode || "total";
        const mode = ["total", "focused", "unfocused"].includes(defaultMode) ? defaultMode : "total";
        this.statsLegendMode = mode;

        const focusColor = mode === "unfocused" ? "#e5e7eb" : "#10b981";
        const unfocusColor = mode === "focused" ? "#e5e7eb" : "#ef4444";

        const slicesData = [
            { label: "Focused", value: focusedSeconds, color: focusColor },
            { label: "Not Focused", value: unfocusedSeconds, color: unfocusColor },
        ];

        const setCenterLabel = (nextMode) => {
            this.statsLegendMode = nextMode;
            if (nextMode === "focused") {
                totalEl.textContent = this.fmtDuration(focusedSeconds);
                totalText.textContent = `Focused (${focusPercent.toFixed(0)}%)`;
            } else if (nextMode === "unfocused") {
                totalEl.textContent = this.fmtDuration(unfocusedSeconds);
                totalText.textContent = `Not Focused (${unfocusPercent.toFixed(0)}%)`;
            } else {
                totalEl.textContent = this.fmtDuration(totalSeconds);
                totalText.textContent = "Total Time";
            }
        };

        setCenterLabel(mode);

        let offset = 0;
        pie.style.background = `conic-gradient(${slicesData
            .map(({ value, color }) => {
                const percent = (value / totalSeconds) * 100;
                const start = offset;
                offset += percent;
                return `${color} ${start}% ${offset}%`;
            })
            .join(", ")})`;

        if (legend) {
            legend.innerHTML = "";
            const createLegendButton = (label, percent, color, nextMode) => {
                const btn = document.createElement("button");
                btn.type = "button";
                const isActive = this.statsLegendMode === nextMode;
                btn.className = `w-full flex items-center justify-between text-sm rounded-lg px-2 py-1 transition-colors ${
                    isActive
                        ? "bg-primary/10 dark:bg-primary-dark/10 text-primary dark:text-primary-dark"
                        : "hover:bg-gray-50 dark:hover:bg-gray-700 text-gray-600 dark:text-gray-300"
                }`;
                btn.setAttribute("aria-pressed", isActive ? "true" : "false");
                btn.innerHTML = `
                    <div class="flex items-center gap-2">
                        <span class="w-3 h-3 rounded-full" style="background:${color}"></span>
                        <span>${label}</span>
                    </div>
                    <span class="font-medium text-gray-900 dark:text-white">${percent.toFixed(1)}%</span>
                `;
                btn.addEventListener("click", () => {
                    const next = this.statsLegendMode === nextMode ? "total" : nextMode;
                    this.statsLegendMode = next;
                    setCenterLabel(next);
                    this.updateDailyFocus();
                });
                return btn;
            };

            legend.appendChild(createLegendButton("Focused", focusPercent, "#10b981", "focused"));
            legend.appendChild(createLegendButton("Not Focused", unfocusPercent, "#ef4444", "unfocused"));
        }

        if (loadingEl) loadingEl.style.display = "none";

        // update
        this.updateDailyActivities();
    }

    // Count daily non-task activities (notes, meetings, emails)
    async updateDailyActivities() {
        const tasksRes = await fetch("/api/v1/task/recurring_tasks", { cache: "no-store" });
        if (!tasksRes.ok) {
            const text = await tasksRes.text().catch(() => "");
            console.error(`Failed to fetch recurring tasks (status: ${tasksRes.status}): ${text}`);
            return; // stops execution
        }

        const tasks = await tasksRes.json();

        const container = document.getElementById("daily-classes");
        container.innerHTML = "";

        if (!tasks.length) {
            // Show placeholder when no tasks
            const placeholder = document.createElement("div");
            placeholder.className = "p-4 text-sm text-gray-500 dark:text-gray-400 italic";
            placeholder.textContent = "No recurring tasks added yet.";
            container.appendChild(placeholder);
            return;
        }

        tasks.forEach((task) => {
            const taskDiv = document.createElement("div");
            taskDiv.className =
                "flex items-center justify-between p-4 rounded-lg shadow-sm border-b border-gray-200 dark:border-gray-700";

            taskDiv.innerHTML = `
            <div class="flex items-center gap-3 min-w-0">
                <div class="h-8 w-8 rounded bg-${task.color}-50 text-${task.color}-600 dark:bg-${task.color}-500/10 dark:text-${task.color}-400 flex items-center justify-center">
                    <span class="material-symbols-outlined text-[20px]">${task.icon}</span>
                </div>
                <div class="min-w-0">
                    <p class="text-sm font-medium truncate text-gray-800 dark:text-gray-100">
                        ${task.name}
                    </p>
                </div>
            </div>
            <div class="flex items-center gap-2">
                <span class="text-xs font-mono text-primary bg-primary/5 dark:bg-primary/10 px-2 py-1 rounded border border-primary/10 dark:border-primary/20">
                    0m
                </span>
                <button class="exclude-btn hover:text-red-600 rounded hover:cursor-pointer">
                    <span class="material-symbols-outlined text-[18px]">delete</span>
                </button>
            </div>
        `;

            // Exclude button handler
            const btn = taskDiv.querySelector(".exclude-btn");
            btn.addEventListener("click", async () => {
                try {
                    await this.excludeDailyActivity(task.name);
                    taskDiv.remove();
                } catch (err) {
                    console.error("Failed to exclude task:", err);
                }
            });

            container.appendChild(taskDiv);
        });
    }

    // Example function to mark task as excluded on server
    async excludeDailyActivity(taskName) {
        try {
            const url = `/api/v1/task/recurring_tasks?name=${encodeURIComponent(taskName)}`;
            const res = await fetch(url, { method: "DELETE" });

            if (!res.ok) {
                const errorText = await res.text();
                console.error("Failed to exclude task:", taskName, errorText);
            }
        } catch (err) {
            console.error("Error excluding task:", err);
        }
    }

    // ==================== STATE MANAGEMENT ====================
    async setCurrentTaskId(taskId) {
        this.currentTaskId = taskId ? String(taskId) : null;

        // Save to localStorage
        if (this.currentTaskId) {
            localStorage.setItem("currentTaskId", this.currentTaskId);
        } else {
            localStorage.removeItem("currentTaskId");
        }

        // Send to server
        try {
            if (this.currentTaskId) {
                const response = await fetch("/api/v1/task/set_current", {
                    method: "POST",
                    headers: {
                        "Content-Type": "application/json",
                    },
                    body: JSON.stringify({ id: this.currentTaskId }),
                });

                if (!response.ok) {
                    const text = await response.text();
                    console.error("[CLIENT] Failed to set current task:", text);
                }
            }
        } catch (err) {
            console.error("[CLIENT] Error sending task to server:", err);
        }

        // Update UI
        this.renderTasks(this.lastTasks);
        this.renderCurrentTask(this.lastTasks);
        this.updateFocusWarning(null, this.lastTasks);
    }

    normalizeAllowList(list) {
        return (Array.isArray(list) ? list : []).map((item) => String(item || "").trim()).filter(Boolean);
    }

    isFocusAllowed(current, task) {
        if (!current || !task) return true;
        const appId = String(current.app_id || "").trim();
        const title = String(current.title || "").trim();

        const allowedApps = this.normalizeAllowList(task.allowed_app_ids);
        const allowedTitles = this.normalizeAllowList(task.allowed_titles);

        const appMatch =
            allowedApps.length > 0 && allowedApps.some((allowed) => allowed.toLowerCase() === appId.toLowerCase());
        const titleMatch =
            allowedTitles.length > 0 &&
            allowedTitles.some((allowed) => title.toLowerCase().includes(allowed.toLowerCase()));

        if (!allowedApps.length && !allowedTitles.length) return true;
        if (allowedApps.length && allowedTitles.length) return appMatch || titleMatch;
        if (allowedApps.length) return appMatch;
        return titleMatch;
    }

    groupTasksByPriority(tasks) {
        const grouped = new Map();

        (Array.isArray(tasks) ? tasks : []).forEach((task) => {
            const raw = typeof task.priority.name === "string" ? task.priority.name.trim() : "";
            const key = /^P\d+$/.test(raw) ? raw : "No priority";

            if (!grouped.has(key)) grouped.set(key, []);
            grouped.get(key).push(task);
        });

        // sort by numeric part: P0, P1, P2...
        return new Map(
            [...grouped.entries()].sort(([a], [b]) => {
                if (a === "No priority") return 1;
                if (b === "No priority") return -1;
                return parseInt(a.slice(1), 10) - parseInt(b.slice(1), 10);
            }),
        );
    }

    normalizeHistoryItems(history, events) {
        const source = Array.isArray(history) && history.length ? history : Array.isArray(events) ? events : [];
        return source
            .map((item) => {
                const duration = Number(item.duration || item.total_duration || 0);
                let start = this.normalizeTimestamp(item.start_time || item.start || item.started_at);
                let end = this.normalizeTimestamp(item.end_time || item.end || item.ended_at || item.last_end);

                if (!end && item.last_end) {
                    end = this.normalizeTimestamp(item.last_end);
                }

                if (!start && end && duration) {
                    start = Math.max(0, end - duration);
                }

                if (!start && !end && !duration) return null;

                const safeEnd = end || (start ? start + duration : 0);

                return {
                    start,
                    end: safeEnd,
                    duration: duration || Math.max(0, safeEnd - start),
                    task: item.task || item.name || item.title || "",
                    category: item.category || "",
                    app_id: item.app_id || "",
                    title: item.title || "",
                };
            })
            .filter((item) => item && (item.start || item.end || item.duration));
    }

    // ==================== RENDER METHODS ====================

    updateCategorySuggestions(categories) {
        this.lastCategories = categories || [];
        const container = document.getElementById("category-menu-items");
        if (!container) return;
        container.innerHTML = "";
        const items = this.lastCategories.length ? this.lastCategories : [{ category: "(no categories)" }];
        items.forEach((c) => {
            const row = document.createElement("button");
            row.type = "button";
            row.className = "w-full text-left px-3 py-2 text-sm hover:bg-gray-50 dark:hover:bg-gray-700";
            row.textContent = c.category;
            if (c.category === "(no categories)") {
                row.disabled = true;
                row.className = "w-full text-left px-3 py-2 text-sm text-gray-400 dark:text-gray-500";
            } else {
                row.addEventListener("click", () => {
                    const input = document.getElementById("task-category");
                    if (input) input.value = c.category;
                    this.applyCategoryDefaults(c.category);
                    this.hideCategoryMenu();
                });
            }
            container.appendChild(row);
        });
    }

    applyCategoryDefaults(categoryValue) {
        const match = this.lastCategories.find(
            (c) => String(c.category).toLowerCase() === String(categoryValue).toLowerCase(),
        );
        if (!match) return;
        if (Array.isArray(match.allowed_app_ids)) {
            this.allowedApps = [...match.allowed_app_ids];
        }
        if (Array.isArray(match.allowed_titles)) {
            this.allowedTitles = [...match.allowed_titles];
        }
        this.renderAllowedLists();
    }

    renderAllowedLists() {
        const apps = document.getElementById("allowed-app-list");
        const titles = document.getElementById("allowed-title-list");
        if (apps) {
            apps.innerHTML = "";
            this.allowedApps.forEach((app, idx) => {
                const chip = document.createElement("button");
                chip.type = "button";
                chip.className =
                    "px-2 py-1 rounded-full text-xs bg-gray-100 dark:bg-gray-700 shadow-sm text-gray-700 dark:text-gray-200";
                chip.innerHTML = `${this.escapeHtml(app)} <span class="ml-1">×</span>`;
                chip.addEventListener("click", () => {
                    this.allowedApps = this.allowedApps.filter((_, i) => i !== idx);
                    this.renderAllowedLists();
                });
                apps.appendChild(chip);
            });
        }
        if (titles) {
            titles.innerHTML = "";
            this.allowedTitles.forEach((title, idx) => {
                const chip = document.createElement("button");
                chip.type = "button";
                chip.className =
                    "px-2 py-1 rounded-full text-xs bg-gray-100 dark:bg-gray-700 shadow-sm text-gray-700 dark:text-gray-200";
                chip.innerHTML = `${this.escapeHtml(title)} <span class="ml-1">×</span>`;
                chip.addEventListener("click", () => {
                    this.allowedTitles = this.allowedTitles.filter((_, i) => i !== idx);
                    this.renderAllowedLists();
                });
                titles.appendChild(chip);
            });
        }
    }

    showCategoryMenu() {
        const menu = document.getElementById("category-menu");
        const panel = document.getElementById("category-panel");
        if (panel && !panel.classList.contains("hidden")) return;
        if (menu) menu.classList.remove("hidden");
    }

    hideCategoryMenu() {
        const menu = document.getElementById("category-menu");
        if (menu) menu.classList.add("hidden");
    }

    renderTasks(tasks) {
        const container = document.getElementById("tasks-container");
        if (!container) return;
        container.innerHTML = "";

        const grouped = this.groupTasksByPriority(tasks);

        for (const [category, items] of grouped.entries()) {
            const section = document.createElement("div");
            section.className = "flex flex-col gap-2";

            const title = document.createElement("h1");
            title.className = "text-lg font-bold uppercase tracking-wide text-gray-800 dark:text-gray-100 pl-1";

            const categoryColor = items[0].category?.color;
            if (categoryColor) {
                title.style.color = `var(--anytype-color-tag-${categoryColor})`;
            }

            title.textContent = category;
            section.appendChild(title);

            const list = document.createElement("ul");
            list.className = "space-y-2";

            items.forEach((task) => {
                const done = !!task.done;
                const isCurrent = this.currentTaskId && String(task.id) === String(this.currentTaskId);

                const li = document.createElement("li");
                li.className = "flex flex-col gap-1 bg-white dark:bg-gray-800 shadow-sm rounded-lg px-3 py-2";
                li.dataset.taskId = task.id;

                const row = document.createElement("div");
                row.className = "flex items-center gap-2";

                const mark = document.createElement("span");
                mark.className = `font-mono text-xs ${
                    done ? "text-emerald-600 dark:text-emerald-400" : "text-gray-400 dark:text-gray-500"
                }`;
                mark.textContent = done ? "[x]" : "[ ]";

                const text = document.createElement("span");
                text.className = `text-sm font-medium ${
                    done ? "text-gray-400 dark:text-gray-500 line-through" : "text-gray-800 dark:text-white"
                }`;
                text.textContent = task.title || "(task)";

                const spacer = document.createElement("span");
                spacer.className = "flex-1";

                const currentBtn = document.createElement("button");
                currentBtn.type = "button";
                currentBtn.className = `h-7 w-7 rounded shadow-sm ${
                    isCurrent
                        ? "bg-emerald-500 border-emerald-500 text-white"
                        : "border-gray-200 dark:border-gray-600 text-gray-400 dark:text-gray-500 hover:text-emerald-500"
                } flex items-center justify-center transition-all`;
                currentBtn.title = isCurrent ? "Current task" : "Set as current";
                currentBtn.innerHTML = `<span class="material-symbols-outlined text-[16px]">${
                    isCurrent ? "radio_button_checked" : "radio_button_unchecked"
                }</span>`;
                currentBtn.dataset.taskId = task.id;
                currentBtn.dataset.taskCurrent = isCurrent ? "true" : "false";

                row.appendChild(mark);
                row.appendChild(text);
                row.appendChild(spacer);
                row.appendChild(currentBtn);

                const debug = document.createElement("div");
                debug.className = "text-[10px] text-gray-500 dark:text-gray-400 flex flex-wrap gap-2";

                const allowedApps = this.normalizeAllowList(task.allowed_app_ids);
                const allowedTitles = this.normalizeAllowList(task.allowed_titles);

                const allowedChip = document.createElement("span");
                allowedChip.className =
                    "px-2 py-0.5 rounded bg-gray-50 dark:bg-gray-700 border border-gray-100 dark:border-gray-600";
                allowedChip.textContent = `Allowed apps: ${allowedApps.length ? allowedApps.join(", ") : "Any"}`;

                const titleChip = document.createElement("span");
                titleChip.className =
                    "px-2 py-0.5 rounded bg-gray-50 dark:bg-gray-700 border border-gray-100 dark:border-gray-600";
                titleChip.textContent = `Allowed titles: ${allowedTitles.length ? allowedTitles.join(", ") : "Any"}`;

                debug.appendChild(allowedChip);
                debug.appendChild(titleChip);

                if (this.lastCurrentFocus) {
                    const focusAllowed = this.isFocusAllowed(this.lastCurrentFocus, task);

                    const focusChip = document.createElement("span");
                    focusChip.className = `px-2 py-0.5 rounded border ${
                        focusAllowed
                            ? "bg-emerald-50 dark:bg-emerald-900/20 border-emerald-200 dark:border-emerald-700 text-emerald-700 dark:text-emerald-300"
                            : "bg-rose-50 dark:bg-rose-900/20 border-rose-200 dark:border-rose-700 text-rose-700 dark:text-rose-300"
                    }`;
                    focusChip.textContent = focusAllowed ? "Current app: focused" : "Current app: NOT ";

                    debug.appendChild(focusChip);
                }

                li.appendChild(row);
                li.appendChild(debug);

                const priorityColor = task.priority?.color;
                if (priorityColor) {
                    li.style.setProperty("--p", `var(--anytype-color-tag-${priorityColor})`);
                    li.classList.add(
                        "bg-[color:var(--p)]/15",
                        "dark:bg-[color:var(--p)]/15",
                        "border-l-4",
                        "border-[color:var(--p)]",
                    );
                }

                if (isCurrent && task.markdown) {
                    setTimeout(() => {
                        this.renderTaskMarkdown(task);
                    }, 0);
                }

                list.appendChild(li);
            });

            section.appendChild(list);
            container.appendChild(section);
        }
    }

    async renderTaskMarkdown(task) {
        if (!task?.markdown) return;

        const markdown = task.markdown;
        const match = markdown.match(/## TO-DO[\s\S]*$/);
        if (!match) return null;

        // Extract TO-DO section and clean completed items
        const parsedMarkdown = match[0]
            .replace(/^\s*-\s*\[x\].*\n?/gim, "")
            .replace(/\n{3,}/g, "\n\n")
            .trim();

        // Find the task element
        const taskElement = document.querySelector(`[data-task-id="${task.id}"]`);
        if (!taskElement) {
            console.warn(`Task element not found for task ${task.id}`);
            return;
        }

        // Convert markdown to HTML
        const html = this.convertMarkdownToHtml(parsedMarkdown);

        // Remove any existing markdown container
        const existingContainer = taskElement.querySelector(".task-markdown-container");
        if (existingContainer) {
            existingContainer.remove();
        }

        // Create and append the markdown container
        const markdownContainer = document.createElement("div");
        markdownContainer.className = "task-markdown-container mt-3 pt-3 border-t border-gray-100 dark:border-gray-700";
        markdownContainer.innerHTML = html;
        taskElement.appendChild(markdownContainer);
    }
    convertMarkdownToHtml(markdown) {
        // First, unescape escaped characters that markdown would normally handle
        // Replace \_ with _, \* with *, etc.
        let processedMarkdown = markdown
            .replace(/\\_/g, "_")
            .replace(/\\\*/g, "*")
            .replace(/\\`/g, "`")
            .replace(/\\\[/g, "[")
            .replace(/\\\]/g, "]")
            .replace(/\\\(/g, "(")
            .replace(/\\\)/g, ")")
            .replace(/\\{/g, "{")
            .replace(/\\}/g, "}")
            .replace(/\\#/g, "#")
            .replace(/\\\+/g, "+")
            .replace(/\\-/g, "-")
            .replace(/\\\./g, ".")
            .replace(/\\!/g, "!");

        // Then process inline code blocks
        let html = processedMarkdown.replace(/`([^`]+)`/g, (match, code) => {
            // Unescape any remaining escaped characters in code
            const unescapedCode = code.replace(/\\_/g, "_").replace(/\\\*/g, "*").replace(/\\`/g, "`");
            const escapedCode = this.escapeHtml(unescapedCode);
            return `<code class="bg-gray-100 dark:bg-gray-700 px-1 py-0.5 rounded text-sm font-mono">${escapedCode}</code>`;
        });

        // Then process multiline code blocks (triple backticks)
        html = html.replace(/```([\s\S]*?)```/g, (match, code) => {
            // Unescape any remaining escaped characters in code blocks
            const unescapedCode = code.replace(/\\_/g, "_").replace(/\\\*/g, "*").replace(/\\`/g, "`");
            const escapedCode = this.escapeHtml(unescapedCode.trim());
            return `<pre class="bg-gray-50 dark:bg-gray-800 p-3 rounded-lg overflow-x-auto my-2"><code class="text-sm font-mono">${escapedCode}</code></pre>`;
        });

        // Process headers (h1, h2, h3)
        html = html.replace(/^### (.*$)/gm, (match, content) => {
            // Don't escape the entire content as it may contain HTML from code blocks
            // Just escape any remaining plain text
            const processedContent = content.replace(
                /(?<!<\/?code[^>]*>)(?<!<\/?strong[^>]*>)(?<!<\/?em[^>]*>)[^<>]+/g,
                (text) => this.escapeHtml(text),
            );
            return `<h3 class="text-sm font-semibold text-gray-800 dark:text-gray-200 mt-3 mb-1">${processedContent}</h3>`;
        });

        html = html.replace(/^## (.*$)/gm, (match, content) => {
            const processedContent = content.replace(
                /(?<!<\/?code[^>]*>)(?<!<\/?strong[^>]*>)(?<!<\/?em[^>]*>)[^<>]+/g,
                (text) => this.escapeHtml(text),
            );
            return `<h2 class="text-base font-bold text-gray-900 dark:text-white mt-4 mb-2">${processedContent}</h2>`;
        });

        html = html.replace(/^# (.*$)/gm, (match, content) => {
            const processedContent = content.replace(
                /(?<!<\/?code[^>]*>)(?<!<\/?strong[^>]*>)(?<!<\/?em[^>]*>)[^<>]+/g,
                (text) => this.escapeHtml(text),
            );
            return `<h1 class="text-lg font-bold text-gray-900 dark:text-white mt-5 mb-3">${processedContent}</h1>`;
        });

        // Process bold text (**text** or __text__) - but only if not inside code blocks
        html = html.replace(/\*\*(.*?)\*\*|__(.*?)__/g, (match, p1, p2) => {
            const content = p1 || p2;
            // Skip if inside a code tag
            if (match.includes("<code") || match.includes("</code>")) {
                return match;
            }
            const escapedContent = this.escapeHtml(content);
            return `<strong class="font-semibold">${escapedContent}</strong>`;
        });

        // Process italic text (*text* or _text_) - but only if not inside code blocks
        html = html.replace(/\*(.*?)\*|_(.*?)_/g, (match, p1, p2) => {
            const content = p1 || p2;
            // Skip if inside a code tag
            if (match.includes("<code") || match.includes("</code>")) {
                return match;
            }
            const escapedContent = this.escapeHtml(content);
            return `<em class="italic">${escapedContent}</em>`;
        });

        // Process unordered lists (including checkbox items)
        // This needs to handle HTML that's already been inserted (like code blocks)
        html = html.replace(/^\s*[-*+] (\[[ x]\]\s*)?(.*$)/gm, (match, checkbox, content) => {
            let checkboxHtml = "";
            if (checkbox) {
                const isChecked = checkbox.includes("x");
                checkboxHtml = `<span class="pl-1 inline-flex items-center justify-center h-6 mr-3 font-mono text-sm ${isChecked ? "text-emerald-600 font-semibold" : "text-gray-500"}">${isChecked ? "[✓]" : "[ ]"}</span>`;
            } else {
                checkboxHtml = '<span class="inline-flex items-center justify-center h-6 mr-3 text-gray-500">•</span>';
            }
            // If content already contains HTML (like from code blocks), don't escape it
            let contentHtml = content || "";
            // Only escape parts that aren't already HTML
            if (contentHtml && !contentHtml.includes("<")) {
                // Unescape any markdown escapes before escaping for HTML
                const unescapedContent = contentHtml
                    .replace(/\\_/g, "_")
                    .replace(/\\\*/g, "*")
                    .replace(/\\`/g, "`")
                    .replace(/\\\[/g, "[")
                    .replace(/\\\]/g, "]");
                contentHtml = this.escapeHtml(unescapedContent.trim());
            }

            return `<li class="ml-4 pl-1 text-sm text-gray-700 dark:text-gray-300 flex items-start">${checkboxHtml}<span class="flex-1">${contentHtml}</span></li>`;
        });

        // Wrap consecutive list items in <ul>
        const lines = html.split("\n");
        let inList = false;
        let resultLines = [];

        for (let i = 0; i < lines.length; i++) {
            if (lines[i].startsWith("<li")) {
                if (!inList) {
                    resultLines.push('<ul class="list-disc pl-5 my-2 space-y-1">');
                    inList = true;
                }
                resultLines.push(lines[i]);
            } else {
                if (inList) {
                    resultLines.push("</ul>");
                    inList = false;
                }
                resultLines.push(lines[i]);
            }
        }

        if (inList) {
            resultLines.push("</ul>");
        }

        html = resultLines.join("\n");

        // Process paragraphs (lines that aren't HTML tags)
        const finalLines = html.split("\n");
        const processedLines = [];
        let currentParagraph = [];

        for (let i = 0; i < finalLines.length; i++) {
            const line = finalLines[i].trim();

            if (!line) {
                if (currentParagraph.length > 0) {
                    const paragraphText = currentParagraph.join("\n");
                    if (!paragraphText.startsWith("<")) {
                        // Unescape markdown escapes in plain text paragraphs too
                        const unescapedText = paragraphText
                            .replace(/\\_/g, "_")
                            .replace(/\\\*/g, "*")
                            .replace(/\\`/g, "`");
                        const escapedText = this.escapeHtml(unescapedText);
                        processedLines.push(
                            `<p class="text-sm text-gray-700 dark:text-gray-300 my-2">${escapedText}</p>`,
                        );
                    } else {
                        processedLines.push(paragraphText);
                    }
                    currentParagraph = [];
                }
                continue;
            }

            if (line.startsWith("<")) {
                if (currentParagraph.length > 0) {
                    const paragraphText = currentParagraph.join("\n");
                    if (!paragraphText.startsWith("<")) {
                        const unescapedText = paragraphText
                            .replace(/\\_/g, "_")
                            .replace(/\\\*/g, "*")
                            .replace(/\\`/g, "`");
                        const escapedText = this.escapeHtml(unescapedText);
                        processedLines.push(
                            `<p class="text-sm text-gray-700 dark:text-gray-300 my-2">${escapedText}</p>`,
                        );
                    } else {
                        processedLines.push(paragraphText);
                    }
                    currentParagraph = [];
                }
                processedLines.push(line);
            } else {
                currentParagraph.push(line);
            }
        }

        // Handle any remaining paragraph
        if (currentParagraph.length > 0) {
            const paragraphText = currentParagraph.join("\n");
            if (!paragraphText.startsWith("<")) {
                const unescapedText = paragraphText.replace(/\\_/g, "_").replace(/\\\*/g, "*").replace(/\\`/g, "`");
                const escapedText = this.escapeHtml(unescapedText);
                processedLines.push(`<p class="text-sm text-gray-700 dark:text-gray-300 my-2">${escapedText}</p>`);
            } else {
                processedLines.push(paragraphText);
            }
        }

        // Wrap in a container with proper styling
        const finalHtml = `
        <div class="task-markdown-content prose prose-sm max-w-none">
            ${processedLines.join("\n")}
        </div>
    `;

        return finalHtml;
    }

    // Add this helper method to update button states
    updateCurrentButtonStates() {
        // Update all current buttons to show correct state
        document.querySelectorAll("[data-task-id]").forEach((taskElement) => {
            const taskId = taskElement.dataset.taskId;
            const isCurrent = this.currentTaskId && String(taskId) === String(this.currentTaskId);
            const currentBtn = taskElement.querySelector("button[data-task-id]");

            if (currentBtn) {
                currentBtn.className = `h-7 w-7 rounded shadow-sm ${isCurrent ? "bg-emerald-500 border-emerald-500 text-white" : "border-gray-200 text-gray-400 hover:text-emerald-500"} flex items-center justify-center transition-all`;
                currentBtn.title = isCurrent ? "Current task" : "Set as current";
                currentBtn.innerHTML = `<span class="material-symbols-outlined text-[16px]">${isCurrent ? "radio_button_checked" : "radio_button_unchecked"}</span>`;
                currentBtn.dataset.taskCurrent = isCurrent ? "true" : "false";
            }
        });

        // Update the current task label
        this.renderCurrentTask(this.lastTasks);
    }

    renderCurrentStatus(current) {
        const status = document.getElementById("current-status");
        if (!status) return;

        const activeDot = document.getElementById("activity-indicator-active");
        const idleDot = document.getElementById("activity-indicator-idle");

        if (!this.monitoringEnabled) {
            status.textContent = "Not Monitoring Activities";
            if (activeDot) activeDot.classList.toggle("hidden", true);
            if (idleDot) idleDot.classList.toggle("hidden", false);
            return;
        }

        if (!current) {
            status.textContent = "Idle";
            if (activeDot) activeDot.classList.toggle("hidden", true);
            if (idleDot) idleDot.classList.toggle("hidden", false);
            return;
        }

        if (activeDot) activeDot.classList.toggle("hidden", false);
        if (idleDot) idleDot.classList.toggle("hidden", true);
        const app = current.app_id || "(unknown)";
        const title = current.title || "(untitled)";
        status.textContent = `${app} — ${title}`;
    }

    renderCurrentTask(tasks) {
        const label = document.getElementById("current-task");
        if (!label) return;
        const match = Array.isArray(tasks) ? tasks.find((t) => String(t.id) === String(this.currentTaskId)) : null;
        if (!match) {
            label.textContent = "Current task: None";
            return;
        }
        label.textContent = `Current task: ${match.title || "(task)"}`;
    }

    getAnytypeWarningElement() {
        let warning = document.getElementById("anytype-warning");
        if (warning) return warning;

        const focusWarning = document.getElementById("focus-warning");
        const container = focusWarning?.parentElement;
        if (!container) return null;

        warning = document.createElement("div");
        warning.id = "anytype-warning";
        warning.className = "hidden mt-2 text-xs font-semibold text-amber-600";
        warning.textContent = "";

        if (focusWarning && focusWarning.nextSibling) {
            container.insertBefore(warning, focusWarning.nextSibling);
        } else {
            container.appendChild(warning);
        }

        return warning;
    }

    updateAnytypeWarning() {
        const warning = this.getAnytypeWarningElement();
        if (!warning) return;

        const missing =
            typeof this.anytypeError === "string" &&
            /missing\s+anytype\s+api\s+key\s+or\s+space\s+id/i.test(this.anytypeError);

        if (missing) {
            warning.textContent = "Anytype API is not configured. Open settings to connect.";
            warning.classList.remove("hidden");
        } else {
            warning.textContent = "";
            warning.classList.add("hidden");
        }
    }

    updateFocusWarning(current, tasks) {
        const warning = document.getElementById("focus-warning");
        if (!warning) return;
        const task = Array.isArray(tasks) ? tasks.find((t) => String(t.id) === String(this.currentTaskId)) : null;
        if (!current || !task) {
            warning.classList.add("hidden");
            this.lastFocusWarningKey = "";
            this.amIFocused = true;
            return;
        }

        const allowed = this.isFocusAllowed(current, task);
        if (allowed) {
            warning.classList.add("hidden");
            this.lastFocusWarningKey = "";
            this.amIFocused = true;
            return;
        }

        warning.classList.remove("hidden");
        const appId = String(current.app_id || "").trim() || "(unknown app)";
        const title = String(current.title || "").trim() || "(untitled)";
        const allowedApps = this.normalizeAllowList(task.allowed_app_ids);
        const allowedTitles = this.normalizeAllowList(task.allowed_titles);
        const allowedAppLabel = allowedApps.length ? allowedApps.join(", ") : "Any app";
        const allowedTitleLabel = allowedTitles.length ? allowedTitles.join(", ") : "Any title";
        warning.textContent = `Not focused: ${appId} — ${title}.`;
        this.amIFocused = false;
    }

    async renderHistory() {
        const days = this.historyDays;
        const list = document.getElementById("history-list");
        if (!list) return;

        const res = await fetch(`/api/v1/focus/app-usage?days=${days}`, {
            cache: "no-store",
        });

        if (!res.ok) {
            list.innerHTML = `<div class="text-sm text-gray-400">Failed to load history.</div>`;
            return;
        }

        const data = await res.json();
        const dayKeys = Object.keys(data).sort((a, b) => (a < b ? 1 : -1));
        list.innerHTML = "";

        // Also render the aggregate history pie/legend and goal in the sidebar
        try {
            let focusedTotalSeconds = null;
            try {
                const focusRes = await fetch(`/api/v1/focus/today?days=${days}`, { cache: "no-store" });
                if (focusRes.ok) {
                    const focusData = await focusRes.json();
                    focusedTotalSeconds = Number(focusData?.focused_seconds ?? 0);
                }
            } catch (err) {
                console.warn("Failed to load focused summary", err);
            }

            const aggregated = {};
            Object.keys(data || {}).forEach((d) => {
                const apps = data[d] || {};
                Object.entries(apps).forEach(([appId, titles]) => {
                    let appTotal = 0;
                    Object.values(titles || {}).forEach((sec) => (appTotal += Number(sec || 0)));
                    aggregated[appId] = (aggregated[appId] || 0) + appTotal;
                });
            });

            const pieEl = document.getElementById("history-pie");
            const legendEl = document.getElementById("history-legend");
            const monthLabel = document.getElementById("history-month-label");
            const goalText = document.getElementById("history-goal-text");
            const goalBar = document.getElementById("history-goal-progress");

            const appEntries = Object.entries(aggregated).sort((a, b) => b[1] - a[1]);
            const total = appEntries.reduce((s, [, v]) => s + v, 0) || 0;

            const colors = ["#2563eb", "#a855f7", "#f97316", "#ef4444", "#10b981", "#06b6d4"];
            const slices = [];
            let others = 0;
            appEntries.forEach((entry, idx) => {
                if (idx < 6) slices.push({ app: entry[0], secs: entry[1], color: colors[idx % colors.length] });
                else others += entry[1];
            });
            if (others > 0) slices.push({ app: "Others", secs: others, color: "#94a3b8" });

            if (pieEl) {
                let offset = 0;
                const parts = slices.map((s) => {
                    const pct = total > 0 ? (s.secs / total) * 100 : 0;
                    const start = offset;
                    offset += pct;
                    const end = offset;
                    return `${s.color} ${start}% ${end}%`;
                });
                pieEl.style.background = `conic-gradient(${parts.join(", ")})`;
            }

            if (legendEl) {
                legendEl.innerHTML = "";
                slices.forEach((s) => {
                    const row = document.createElement("div");
                    row.className = "flex items-center justify-between text-sm";
                    const pct = total > 0 ? (s.secs / total) * 100 : 0;
                    row.innerHTML = `
                        <div class=\"flex items-center gap-2\"> 
                            <span class=\"w-3 h-3 rounded-full\" style=\"background:${s.color}\"></span>
                            <span class=\"text-gray-600 dark:text-gray-400\">${this.escapeHtml(s.app)}</span>
                        </div>
                        <div class=\"text-right\"> 
                            <div class=\"font-medium text-gray-900 dark:text-white\">${this.fmtDuration(s.secs)}</div>
                            <div class=\"text-xs text-gray-500\">${pct.toFixed(1)}%</div>
                        </div>
                    `;
                    legendEl.appendChild(row);
                });
            }

            if (monthLabel) {
                const days = Number(this.historyDays) || 30;
                const now = new Date();
                if (days <= 7)
                    monthLabel.textContent = now.toLocaleDateString(undefined, { month: "short", day: "numeric" });
                else monthLabel.textContent = now.toLocaleDateString(undefined, { month: "short", year: "numeric" });
            }

            const goalTotal = Number.isFinite(focusedTotalSeconds) ? focusedTotalSeconds : total;
            if (goalText) goalText.textContent = `${this.fmtDuration(goalTotal)} over ${Object.keys(data).length} days`;
            if (goalBar) {
                const days = Number(this.historyDays) || 30;
                const goalSeconds = days * 3600;
                const pct = goalSeconds > 0 ? Math.min(100, (goalTotal / goalSeconds) * 100) : 0;
                goalBar.style.width = `${pct}%`;
            }
        } catch (err) {
            console.warn("Failed to render history pie/legend", err);
        }

        if (!dayKeys.length) {
            list.innerHTML = `
            <div class="text-sm text-gray-400 dark:text-gray-500">
                No history available yet.
            </div>
        `;
            return;
        }

        dayKeys.forEach((dayKey, index) => {
            const apps = data[dayKey];
            const dayDate = new Date(dayKey + "T00:00:00");

            let dayTotal = 0;
            Object.values(apps).forEach((titles) => {
                Object.values(titles).forEach((sec) => (dayTotal += sec));
            });

            const opacity = Math.max(0.7, 1 - index * 0.05);

            // Card
            const card = document.createElement("div");
            card.className = "bg-white dark:bg-gray-800 shadow-sm rounded-xl overflow-hidden shadow-subtle";
            card.style.opacity = String(opacity);

            // Header
            const header = document.createElement("div");
            header.className = "bg-gray-50 dark:bg-gray-700 px-5 py-3 shadow-sm flex justify-between items-center";

            header.innerHTML = `
            <div class="flex items-center gap-4">
                <div class="flex flex-col">
                    <span class="text-sm font-bold text-gray-900 dark:text-white">
                        ${this.formatDayLabel(dayDate)}
                    </span>
                </div>
                <div class="h-8 w-px bg-gray-200 dark:bg-gray-600"></div>
                <div class="flex items-center gap-1.5">
                    <span class="material-symbols-outlined text-primary text-lg">timer</span>
                    <span class="text-sm font-bold text-primary">
                        ${this.fmtDuration(dayTotal)}
                    </span>
                </div>
            </div>
        `;

            // Body
            const body = document.createElement("div");
            body.className = "p-5";

            const grid = document.createElement("div");
            grid.className = "grid grid-cols-1 sm:grid-cols-2 gap-3";

            Object.entries(apps).forEach(([appId, titles]) => {
                const badgeStyle = this.appIdBadgeStyle(appId);

                let appTotal = 0;
                Object.values(titles).forEach((sec) => (appTotal += sec));

                const appBlock = document.createElement("div");
                appBlock.className =
                    "rounded-lg border border-gray-200 dark:border-gray-600 p-3 bg-gray-50 dark:bg-gray-800";

                appBlock.innerHTML = `
                <div class="flex items-center justify-between mb-2">
                    <span class="px-2 py-0.5 rounded text-[11px] font-bold border"
                        style="background:${badgeStyle.background};
                               color:${badgeStyle.color};
                               border-color:${badgeStyle.borderColor}">
                        ${this.escapeHtml(appId)}
                    </span>
                    <span class="text-xs font-bold text-primary">
                        ${this.fmtDuration(appTotal)}
                    </span>
                </div>
            `;

                const titleList = document.createElement("div");
                titleList.className = "flex flex-col gap-1 pl-2";

                const entries = Object.entries(titles).sort((a, b) => b[1] - a[1]);

                let othersSeconds = 0;

                // first pass: collect < 60s
                entries.forEach(([_, seconds]) => {
                    if (seconds < 60) {
                        othersSeconds += seconds;
                    }
                });

                // second pass: render >= 60s
                entries.forEach(([title, seconds]) => {
                    if (seconds < 60) return;

                    const row = document.createElement("div");
                    row.className = "flex justify-between text-sm text-gray-700 dark:text-gray-200";

                    row.innerHTML = `
                        <span class="truncate" title="${this.escapeHtml(title)}">
                            ${this.escapeHtml(this.truncateText(title, 36))}
                        </span>
                        <span class="text-xs font-mono text-gray-500 dark:text-gray-400">
                            ${this.fmtDuration(seconds)}
                        </span>
                    `;

                    titleList.appendChild(row);
                });

                // append Others if needed
                if (othersSeconds > 0) {
                    const row = document.createElement("div");
                    row.className = "flex justify-between text-sm text-gray-600 dark:text-gray-400 italic";

                    const label =
                        othersSeconds < 60 ? `${Math.round(othersSeconds)}s` : this.fmtDuration(othersSeconds);

                    row.innerHTML = `
        <span>Others</span>
        <span class="text-xs font-mono">${label}</span>
    `;

                    titleList.appendChild(row);
                }

                appBlock.appendChild(titleList);
                grid.appendChild(appBlock);
            });

            body.appendChild(grid);
            card.appendChild(header);
            card.appendChild(body);
            list.appendChild(card);
        });
    }

    // ==================== POMODORO ====================

    initPomodoro() {
        document.getElementById("pomodoro-focus-mode")?.addEventListener("click", () => this.setPomodoroMode("focus"));
        document
            .getElementById("pomodoro-short-break-mode")
            ?.addEventListener("click", () => this.setPomodoroMode("short-break"));
        document
            .getElementById("pomodoro-long-break-mode")
            ?.addEventListener("click", () => this.setPomodoroMode("long-break"));

        // Control buttons
        document.getElementById("pomodoro-start")?.addEventListener("click", () => this.togglePomodoro());
        document.getElementById("pomodoro-pause")?.addEventListener("click", () => this.pausePomodoro());
        document.getElementById("pomodoro-reset")?.addEventListener("click", () => this.resetPomodoro());

        // Sliders
        document.getElementById("focus-duration-slider")?.addEventListener("input", (e) => {
            const minutes = parseInt(e.target.value);
            document.getElementById("focus-duration-value").textContent = `${minutes} min`;
            this.pomodoro.focusDuration = minutes * 60;
            if (this.pomodoro.mode === "focus" && !this.pomodoro.isRunning) {
                this.pomodoro.timeLeft = minutes * 60;
                this.updatePomodoroDisplay();
            }
        });

        document.getElementById("short-break-slider")?.addEventListener("input", (e) => {
            const minutes = parseInt(e.target.value);
            document.getElementById("short-break-value").textContent = `${minutes} min`;
            this.pomodoro.shortBreakDuration = minutes * 60;
            if (this.pomodoro.mode === "short-break" && !this.pomodoro.isRunning) {
                this.pomodoro.timeLeft = minutes * 60;
                this.updatePomodoroDisplay();
            }
        });

        document.getElementById("long-break-slider")?.addEventListener("input", (e) => {
            const minutes = parseInt(e.target.value);
            document.getElementById("long-break-value").textContent = `${minutes} min`;
            this.pomodoro.longBreakDuration = minutes * 60;
            if (this.pomodoro.mode === "long-break" && !this.pomodoro.isRunning) {
                this.pomodoro.timeLeft = minutes * 60;
                this.updatePomodoroDisplay();
            }
        });

        // Auto-start toggle
        document.getElementById("auto-start-breaks")?.addEventListener("change", (e) => {
            this.pomodoro.autoStartBreaks = e.target.checked;
        });

        // Initialize display
        this.updatePomodoroDisplay();
    }

    setPomodoroMode(mode) {
        if (this.pomodoro.isRunning) {
            if (!confirm("Timer is running. Switch mode and reset timer?")) return;
            this.resetPomodoro();
        }

        this.pomodoro.mode = mode;

        // Update button styles
        const focusBtn = document.getElementById("pomodoro-focus-mode");
        const shortBreakBtn = document.getElementById("pomodoro-short-break-mode");
        const longBreakBtn = document.getElementById("pomodoro-long-break-mode");

        [focusBtn, shortBreakBtn, longBreakBtn].forEach((btn) => {
            if (btn) {
                btn.classList.remove(
                    "bg-primary",
                    "dark:bg-primary-dark",
                    "text-white",
                    "shadow-lg",
                    "shadow-primary/20",
                    "dark:shadow-primary-dark/20",
                );
                btn.classList.add(
                    "text-gray-600",
                    "dark:text-gray-400",
                    "hover:text-gray-900",
                    "dark:hover:text-white",
                );
            }
        });

        // Highlight active mode
        const activeBtn = mode === "focus" ? focusBtn : mode === "short-break" ? shortBreakBtn : longBreakBtn;

        if (activeBtn) {
            activeBtn.classList.add(
                "bg-primary",
                "dark:bg-primary-dark",
                "text-white",
                "shadow-lg",
                "shadow-primary/20",
                "dark:shadow-primary-dark/20",
            );
            activeBtn.classList.remove(
                "text-gray-600",
                "dark:text-gray-400",
                "hover:text-gray-900",
                "dark:hover:text-white",
            );
        }

        // Set timer based on mode
        switch (mode) {
            case "focus":
                this.pomodoro.timeLeft = this.pomodoro.focusDuration;
                break;
            case "short-break":
                this.pomodoro.timeLeft = this.pomodoro.shortBreakDuration;
                break;
            case "long-break":
                this.pomodoro.timeLeft = this.pomodoro.longBreakDuration;
                break;
        }

        this.updatePomodoroDisplay();
    }

    togglePomodoro() {
        if (!this.pomodoro.isRunning) {
            this.startPomodoro();
        } else {
            this.pausePomodoro();
        }
    }

    startPomodoro() {
        this.pomodoro.isRunning = true;
        this.pomodoro.isPaused = false;

        // Update button states
        document.getElementById("pomodoro-start")?.classList.add("hidden");
        document.getElementById("pomodoro-pause")?.classList.remove("hidden");

        // Start the interval
        this.pomodoro.interval = setInterval(() => {
            this.pomodoro.timeLeft--;

            if (this.pomodoro.timeLeft <= 0) {
                this.pomodoroComplete();
            }

            this.updatePomodoroDisplay();
        }, 1000);
    }

    pausePomodoro() {
        this.pomodoro.isPaused = true;
        clearInterval(this.pomodoro.interval);
        this.pomodoro.interval = null;

        // Update button states
        document.getElementById("pomodoro-start")?.classList.remove("hidden");
        document.getElementById("pomodoro-pause")?.classList.add("hidden");

        const startBtn = document.getElementById("pomodoro-start");
        if (startBtn) {
            startBtn.innerHTML = `
            <span class="material-symbols-outlined text-2xl" style="font-variation-settings: 'FILL' 1">
                play_arrow
            </span>
            <span class="text-lg font-bold tracking-tight">Resume</span>
        `;
        }
    }

    resetPomodoro() {
        clearInterval(this.pomodoro.interval);
        this.pomodoro.isRunning = false;
        this.pomodoro.isPaused = false;
        this.pomodoro.interval = null;

        // Reset timer to current mode's duration
        switch (this.pomodoro.mode) {
            case "focus":
                this.pomodoro.timeLeft = this.pomodoro.focusDuration;
                break;
            case "short-break":
                this.pomodoro.timeLeft = this.pomodoro.shortBreakDuration;
                break;
            case "long-break":
                this.pomodoro.timeLeft = this.pomodoro.longBreakDuration;
                break;
        }

        // Update button states
        document.getElementById("pomodoro-start")?.classList.remove("hidden");
        document.getElementById("pomodoro-pause")?.classList.add("hidden");

        const startBtn = document.getElementById("pomodoro-start");
        if (startBtn) {
            startBtn.innerHTML = `
            <span class="material-symbols-outlined text-2xl" style="font-variation-settings: 'FILL' 1">
                play_arrow
            </span>
            <span class="text-lg font-bold tracking-tight">Start Timer</span>
        `;
        }

        this.updatePomodoroDisplay();
    }

    pomodoroComplete() {
        clearInterval(this.pomodoro.interval);
        this.pomodoro.isRunning = false;

        // Play sound
        this.playTimerEndSound();

        // Show notification
        this.showPomodoroNotification();

        // Auto-start next session if enabled
        if (this.pomodoro.autoStartBreaks && this.pomodoro.mode === "focus") {
            setTimeout(() => {
                this.setPomodoroMode("short-break");
                this.startPomodoro();
            }, 1000);
        } else if (this.pomodoro.autoStartBreaks && this.pomodoro.mode !== "focus") {
            setTimeout(() => {
                this.setPomodoroMode("focus");
                this.startPomodoro();
            }, 1000);
        } else {
            this.resetPomodoro();
        }
    }

    updatePomodoroDisplay() {
        const timerElement = document.getElementById("pomodoro-timer");
        const phaseElement = document.getElementById("pomodoro-phase");
        const progressElement = document.getElementById("pomodoro-progress");

        if (!timerElement || !phaseElement) return;

        // Update timer display
        const minutes = Math.floor(this.pomodoro.timeLeft / 60);
        const seconds = this.pomodoro.timeLeft % 60;
        timerElement.textContent = `${minutes.toString().padStart(2, "0")}:${seconds.toString().padStart(2, "0")}`;

        // Update phase text
        switch (this.pomodoro.mode) {
            case "focus":
                phaseElement.textContent = "Focus";
                phaseElement.className =
                    "text-gray-600 dark:text-gray-400 uppercase tracking-[0.4em] text-xs mt-4 font-semibold";
                break;
            case "short-break":
                phaseElement.textContent = "Short Break";
                phaseElement.className =
                    "text-emerald-600 dark:text-emerald-400 uppercase tracking-[0.4em] text-xs mt-4 font-semibold";
                break;
            case "long-break":
                phaseElement.textContent = "Long Break";
                phaseElement.className =
                    "text-blue-600 dark:text-blue-400 uppercase tracking-[0.4em] text-xs mt-4 font-semibold";
                break;
        }

        // Update progress ring
        if (progressElement) {
            const totalDuration = this.getCurrentModeDuration();
            const progress = 1 - this.pomodoro.timeLeft / totalDuration;
            const circumference = 2 * Math.PI * 48; // r="48%" = 48% of 200 (circle diameter)
            const offset = circumference * progress;
            progressElement.style.strokeDashoffset = offset;
        }
    }

    getCurrentModeDuration() {
        switch (this.pomodoro.mode) {
            case "focus":
                return this.pomodoro.focusDuration;
            case "short-break":
                return this.pomodoro.shortBreakDuration;
            case "long-break":
                return this.pomodoro.longBreakDuration;
            default:
                return 25 * 60;
        }
    }

    showPomodoroNotification() {
        const message =
            this.pomodoro.mode === "focus"
                ? "Focus session complete! Time for a break."
                : "Break time is over! Ready to focus?";

        // Create or show notification element
        let notification = document.getElementById("pomodoro-notification");
        if (!notification) {
            notification = document.createElement("div");
            notification.id = "pomodoro-notification";
            notification.className = "fixed bottom-4 right-4 p-4 rounded-lg shadow-xl z-50 hidden";
            document.body.appendChild(notification);
        }

        notification.innerHTML = `
        <div class="flex items-center gap-3">
            <span class="material-symbols-outlined text-2xl">timer</span>
            <div>
                <p class="font-semibold">${message}</p>
                <p class="text-sm opacity-75">Pomodoro Timer</p>
            </div>
            <button onclick="this.parentElement.parentElement.classList.add('hidden')" 
                    class="ml-4 text-gray-500 hover:text-gray-700 dark:text-gray-400 dark:hover:text-gray-200">
                <span class="material-symbols-outlined">close</span>
            </button>
        </div>
    `;

        // Style based on theme
        notification.className = notification.className.replace(/bg-\S+ text-\S+/, "");
        if (document.documentElement.classList.contains("dark")) {
            notification.classList.add("bg-gray-800", "text-white", "border", "border-gray-700");
        } else {
            notification.classList.add("bg-white", "text-gray-900", "border", "border-gray-200");
        }

        notification.classList.remove("hidden");

        // Auto-hide after 5 seconds
        setTimeout(() => {
            notification.classList.add("hidden");
        }, 5000);
    }

    // ==================== VIEW MANAGEMENT ====================

    setView(view) {
        this.currentView = view;

        // Get all view containers
        const tasksView = document.getElementById("view-tasks");
        const historyView = document.getElementById("view-history");
        const pomodoroView = document.getElementById("view-pomodoro");

        // Get all sidebar/aside containers
        const tasksAside = document.getElementById("tasks-aside");
        const historyAside = document.getElementById("history-aside");

        // Show/hide main content views
        if (tasksView) tasksView.classList.toggle("hidden", view !== "tasks");
        if (historyView) historyView.classList.toggle("hidden", view !== "history");
        if (pomodoroView) pomodoroView.classList.toggle("hidden", view !== "pomodoro");

        // Show/hide sidebars
        if (tasksAside) {
            tasksAside.classList.toggle("lg:flex", view === "tasks");
            tasksAside.classList.toggle("hidden", view !== "tasks");
        }
        if (historyAside) {
            historyAside.classList.toggle("lg:flex", view === "history");
            historyAside.classList.toggle("hidden", view !== "history");
        }

        // Update navigation link styling
        document.querySelectorAll(".nav-link[data-view]").forEach((link) => {
            const isActive = link.dataset.view === view;

            // Reset all classes first
            link.classList.remove(
                "bg-primary/10",
                "dark:bg-primary-dark/10",
                "text-primary",
                "dark:text-primary-dark",
                "font-semibold",
                "text-gray-500",
                "dark:text-gray-400",
                "hover:bg-gray-50",
                "dark:hover:bg-gray-700",
                "hover:text-gray-900",
                "dark:hover:text-white",
                "hover:bg-primary/20",
                "dark:hover:bg-primary-dark/20",
                "hover:text-primary",
                "dark:hover:text-primary-dark",
            );

            // Apply active styling
            if (isActive) {
                link.classList.add(
                    "bg-primary/10",
                    "dark:bg-primary-dark/10",
                    "text-primary",
                    "dark:text-primary-dark",
                    "font-semibold",
                    "hover:bg-primary/20",
                    "dark:hover:bg-primary-dark/20",
                    "hover:text-primary",
                    "dark:hover:text-primary-dark",
                );
            } else {
                // Apply inactive styling
                link.classList.add(
                    "text-gray-500",
                    "dark:text-gray-400",
                    "hover:bg-gray-50",
                    "dark:hover:bg-gray-700",
                    "hover:text-gray-900",
                    "dark:hover:text-white",
                );
            }
        });

        // Refresh data based on the current view
        if (view === "tasks") {
            this.refreshAll();
        } else if (view === "history") {
            this.refreshAll();
        } else if (view === "pomodoro") {
            // Reset pomodoro display when switching to pomodoro view
            this.updatePomodoroDisplay();

            // Stop any ongoing timer if pomodoro is paused
            if (this.pomodoro.isPaused) {
                this.resetPomodoro();
            }
        }

        // Update page title
        let pageTitle = "Focus Dashboard";
        if (view === "tasks") pageTitle = "Focus Dashboard - Tasks";
        else if (view === "pomodoro") pageTitle = "Focus Dashboard - Pomodoro";
        else if (view === "history") pageTitle = "Focus Dashboard - History";

        document.title = pageTitle;
    }
    isPageActive() {
        return true;
        // return document.visibilityState === "visible" && document.hasFocus();
    }

    async refreshFocusOnly() {
        if (!this.isPageActive()) return;
        const current = await this.loadCurrent();
        this.lastCurrentFocus = current;
        this.renderCurrentStatus(current);
        this.updateFocusWarning(current, this.lastTasks);
        this.updateDailyFocus();
    }

    async renderTotalTimeFocus() {
        const bar = document.getElementById("history-goal-progress");
    }

    async refreshAll() {
        if (this.currentView === "history") {
            const [tasks, current, history, events] = await Promise.all([
                this.loadTasks(),
                this.loadCurrent(),
                this.loadHistory(),
                this.loadEvents(),
            ]);

            // this.updateDailyFocus();
            // this.updateDailyActivities();
            this.lastTasks = Array.isArray(tasks) ? tasks : [];
            this.lastCurrentFocus = current;
            // this.updateAnytypeWarning();
            // this.renderCurrentStatus(current);
            // this.renderCurrentTask(tasks);
            // this.updateFocusWarning(current, tasks);
            //

            this.renderTotalTimeFocus();
            this.renderHistory();
            return;
        }

        const tasks = await this.loadTasks();
        const current = await this.loadCurrent();
        // const categories = await this.loadCategories();

        this.lastTasks = Array.isArray(tasks) ? tasks : [];
        this.lastCurrentFocus = current;
        this.renderTasks(tasks);
        this.updateAnytypeWarning();
        this.renderCurrentStatus(current);
        this.renderCurrentTask(tasks);
        this.updateFocusWarning(current, tasks);
        this.updateDailyActivities();
        this.updateTasksCategories();
    }

    async refreshEverything() {
        const tasks = await this.loadTasks();
        const current = await this.loadCurrent();
        // const categories = await this.loadCategories();
        const history = await this.loadHistory();
        const events = await this.loadEvents();

        this.lastTasks = Array.isArray(tasks) ? tasks : [];
        this.lastCurrentFocus = current;
        this.renderTasks(tasks);
        this.updateAnytypeWarning();
        this.renderCurrentStatus(current);
        this.renderCurrentTask(tasks);
        this.updateFocusWarning(current, tasks);
        // this.updateCategorySuggestions(categories);
        // this.renderStats(history, events, tasks);
        this.renderHistory();
        this.updateDailyActivities();
    }

    // ==================== TASK MANAGEMENT ====================
    async submitTask() {
        const name = document.getElementById("task-name");
        const category = document.getElementById("task-category");
        if (!name || !name.value.trim()) return;

        const payload = {
            task: name.value.trim(),
            category: category?.value.trim() || "",
            start_time: 0,
            end_time: 0,
            allowed_app_ids: this.allowedApps,
            allowed_titles: this.allowedTitles,
            exclude: false,
        };

        const res = await fetch("/api/v1/tasks", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload),
        });

        if (!res.ok) {
            const text = await res.text();
            alert("Failed to save task: " + text);
            return;
        }

        name.value = "";
        this.allowedApps = [];
        this.allowedTitles = [];
        this.renderAllowedLists();
        await this.refreshAll();
    }

    // ==================== TIMER METHODS ====================
    setActivityIndicator(isActive) {
        const activeDot = document.getElementById("activity-indicator-active");
        const idleDot = document.getElementById("activity-indicator-idle");
        if (activeDot) activeDot.classList.toggle("hidden", !isActive);
        if (idleDot) idleDot.classList.toggle("hidden", isActive);
    }

    ensureAudioContext() {
        if (!this.audioContext) {
            const Ctx = window.AudioContext || window.webkitAudioContext;
            if (!Ctx) return null;
            this.audioContext = new Ctx();
        }
        if (this.audioContext.state === "suspended") {
            this.audioContext.resume().catch(() => {});
        }
        return this.audioContext;
    }

    playTimerEndSound() {
        const ctx = this.ensureAudioContext();
        if (!ctx) return;

        const now = ctx.currentTime;
        const duration = 0.9;

        const oscillator = ctx.createOscillator();
        const gain = ctx.createGain();
        const compressor = ctx.createDynamicsCompressor();

        oscillator.type = "sine";
        oscillator.frequency.setValueAtTime(880, now);

        gain.gain.setValueAtTime(0.0001, now);
        gain.gain.exponentialRampToValueAtTime(1.2, now + 0.02); // push hotter
        gain.gain.exponentialRampToValueAtTime(0.0001, now + duration);

        // Compressor settings = louder perceived sound
        compressor.threshold.setValueAtTime(-24, now);
        compressor.knee.setValueAtTime(30, now);
        compressor.ratio.setValueAtTime(12, now);
        compressor.attack.setValueAtTime(0.003, now);
        compressor.release.setValueAtTime(0.25, now);

        oscillator.connect(gain);
        gain.connect(compressor);
        compressor.connect(ctx.destination);

        oscillator.start(now);
        oscillator.stop(now + duration);
    }

    parseDurationInput(value) {
        const v = String(value || "")
            .trim()
            .toLowerCase();
        if (!v) return 0;
        const match = v.match(/(\d+(?:\.\d+)?)([hms])?/);
        if (!match) return 0;
        const num = parseFloat(match[1]);
        const unit = match[2] || "m";
        if (unit === "h") return Math.round(num * 3600);
        if (unit === "s") return Math.round(num);
        return Math.round(num * 60);
    }

    updateTimerDisplay(seconds) {
        const hrs = document.getElementById("timer-hrs");
        const min = document.getElementById("timer-min");
        const sec = document.getElementById("timer-sec");
        const h = Math.floor(seconds / 3600);
        const m = Math.floor((seconds % 3600) / 60);
        const s = Math.floor(seconds % 60);
        if (hrs) hrs.textContent = String(h).padStart(2, "0");
        if (min) min.textContent = String(m).padStart(2, "0");
        if (sec) sec.textContent = String(s).padStart(2, "0");
    }

    stopTimer() {
        this.timerRunning = false;
        if (this.timerInterval) {
            clearInterval(this.timerInterval);
            this.timerInterval = null;
        }
        this.setActivityIndicator(false);
        const button = document.getElementById("record-toggle");
        if (button) {
            const icon = button.querySelector(".material-symbols-outlined");
            const label = button.querySelector("span:not(.material-symbols-outlined)");
            if (icon) icon.textContent = "play_arrow";
            if (label) label.textContent = "Start Focus";
        }
    }

    startTimer(seconds) {
        this.timerSeconds = Math.max(0, seconds);
        this.updateTimerDisplay(this.timerSeconds);
        this.stopTimer();
        if (this.timerSeconds <= 0) return;
        this.timerRunning = true;
        this.setActivityIndicator(true);
        const button = document.getElementById("record-toggle");
        if (button) {
            const icon = button.querySelector(".material-symbols-outlined");
            const label = button.querySelector("span:not(.material-symbols-outlined)");
            if (icon) icon.textContent = "pause";
            if (label) label.textContent = "Pause";
        }
        this.timerInterval = setInterval(() => {
            if (!this.amIFocused) return;
            if (!this.timerRunning) return;
            this.timerSeconds = Math.max(0, this.timerSeconds - 1);
            this.updateTimerDisplay(this.timerSeconds);
            if (this.timerSeconds <= 0) {
                this.playTimerEndSound();
                this.stopTimer();
                const status = document.getElementById("current-status");
                if (status) status.textContent = "Time's up";
            }
        }, 1000);
    }

    // ==================== MODAL METHODS ====================
    openDurationModal() {
        const modal = document.getElementById("duration-modal");
        const input = document.getElementById("duration-modal-input");
        if (input) input.value = this.currentDurationValue || "25m";
        if (modal) {
            modal.classList.remove("hidden");
            modal.classList.add("flex");
            modal.setAttribute("aria-hidden", "false");
        }
        setTimeout(() => input?.focus(), 0);
    }

    closeDurationModal() {
        const modal = document.getElementById("duration-modal");
        if (modal) {
            modal.classList.add("hidden");
            modal.classList.remove("flex");
            modal.setAttribute("aria-hidden", "true");
        }
    }

    saveDurationModal() {
        const input = document.getElementById("duration-modal-input");
        const value = input?.value || "";
        this.currentDurationValue = value || "25m";
        if (!this.timerRunning) {
            const seconds = this.parseDurationInput(this.currentDurationValue);
            this.updateTimerDisplay(seconds);
        }
        this.closeDurationModal();
    }

    openAnytypeConfigModal() {
        const modal = document.getElementById("anytype-config-modal");
        const status = document.getElementById("anytype-config-status");
        const saveButton = document.getElementById("anytype-config-save");
        const apiKeyEl = document.getElementById("anytype-api-key");
        const spaceIdEl = document.getElementById("anytype-space-id");
        if (status) status.textContent = "";
        if (saveButton) {
            const hasManualInputs = !!(apiKeyEl || spaceIdEl);
            saveButton.classList.toggle("hidden", !hasManualInputs);
        }
        if (modal) {
            modal.classList.remove("hidden");
            modal.classList.add("flex");
            modal.setAttribute("aria-hidden", "false");
        }
        setTimeout(() => document.getElementById("anytype-auth-code")?.focus(), 0);
    }

    closeAnytypeConfigModal() {
        const modal = document.getElementById("anytype-config-modal");
        if (modal) {
            modal.classList.add("hidden");
            modal.classList.remove("flex");
            modal.setAttribute("aria-hidden", "true");
        }
    }

    setAnytypeStatus(message) {
        const status = document.getElementById("anytype-config-status");
        if (status) status.textContent = message || "";
    }

    renderAnytypeSpaces(spaces) {
        this.lastAnytypeSpaces = Array.isArray(spaces) ? spaces : [];
        const select = document.getElementById("anytype-space-select");
        if (!select) return;

        select.innerHTML = "";
        if (!this.lastAnytypeSpaces.length) {
            const option = document.createElement("option");
            option.value = "";
            option.textContent = "No spaces available";
            select.appendChild(option);
            return;
        }

        this.lastAnytypeSpaces.forEach((space) => {
            const option = document.createElement("option");
            option.value = space.id || "";
            option.textContent = space.name || space.id || "(space)";
            select.appendChild(option);
        });
    }

    async createAnytypeChallenge() {
        const appName = "FocusService";
        if (!appName) {
            this.setAnytypeStatus("App name is required.");
            return;
        }
        this.setAnytypeStatus("Creating challenge…");
        try {
            const res = await fetch("/api/v1/anytype/auth/challenges", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
            });
            if (!res.ok) {
                const text = await res.text();
                this.setAnytypeStatus(text || "Failed to create challenge.");
                return;
            }
            const data = await res.json();
            this.anytypeChallengeId = data.challenge_id || "";
            const challengeIdEl = document.getElementById("anytype-challenge-id");
            if (challengeIdEl) challengeIdEl.value = this.anytypeChallengeId;
            this.setAnytypeStatus("Challenge created. Enter the 4-digit code from Anytype.");
        } catch (err) {
            console.error("Failed to create Anytype challenge", err);
            this.setAnytypeStatus("Failed to create challenge.");
        }
    }

    async createAnytypeApiKey() {
        const challengeId = this.anytypeChallengeId || "";
        const code = document.getElementById("anytype-auth-code")?.value.trim() || "";
        if (!challengeId || !code) {
            this.setAnytypeStatus("Challenge ID and code are required.");
            return;
        }
        this.setAnytypeStatus("Creating API key…");
        try {
            const res = await fetch("/api/v1/anytype/auth/api_keys", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ challenge_id: challengeId, code }),
            });
            if (!res.ok) {
                const text = await res.text();
                this.setAnytypeStatus(text || "Failed to create API key.");
                return;
            }
            await res.json();
            this.setAnytypeStatus("API key saved. Load spaces to continue.");
            const authCodeEl = document.getElementById("anytype-auth-code");
            if (authCodeEl) authCodeEl.value = "";
            await this.loadAnytypeSpaces();
            this.updateAnytypeWarning();
        } catch (err) {
            console.error("Failed to create Anytype API key", err);
            this.setAnytypeStatus("Failed to create API key.");
        }
    }

    async loadAnytypeSpaces() {
        this.setAnytypeStatus("Loading spaces…");
        try {
            const res = await fetch("/api/v1/anytype/spaces", { cache: "no-store" });
            if (!res.ok) {
                const text = await res.text();
                this.setAnytypeStatus(text || "Failed to load spaces.");
                this.renderAnytypeSpaces([]);
                return;
            }
            const data = await res.json();
            const spaces = Array.isArray(data?.data) ? data.data : [];
            this.renderAnytypeSpaces(spaces);
            this.setAnytypeStatus(spaces.length ? "Spaces loaded." : "No spaces found.");
        } catch (err) {
            console.error("Failed to load Anytype spaces", err);
            this.setAnytypeStatus("Failed to load spaces.");
            this.renderAnytypeSpaces([]);
        }
    }

    async saveAnytypeSpace() {
        const select = document.getElementById("anytype-space-select");
        const spaceId = select?.value || "";
        if (!spaceId) {
            this.setAnytypeStatus("Select a space first.");
            return;
        }
        this.setAnytypeStatus("Saving space…");
        try {
            const res = await fetch("/api/v1/anytype/space", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ space_id: spaceId }),
            });
            if (!res.ok) {
                const text = await res.text();
                this.setAnytypeStatus(text || "Failed to save space.");
                return;
            }
            this.setAnytypeStatus("Space saved. Syncing tasks…");
            await this.refreshAll();
        } catch (err) {
            console.error("Failed to save Anytype space", err);
            this.setAnytypeStatus("Failed to save space.");
        }
    }

    async saveAnytypeConfig() {
        const apiKeyEl = document.getElementById("anytype-api-key");
        const spaceIdEl = document.getElementById("anytype-space-id");
        const status = document.getElementById("anytype-config-status");
        if (!apiKeyEl || !spaceIdEl) {
            if (status) status.textContent = "Use the auth flow to generate a key and select a space.";
            return;
        }
        const api_key = apiKeyEl?.value.trim() || "";
        const space_id = spaceIdEl?.value.trim() || "";
        if (!api_key || !space_id) {
            if (status) status.textContent = "Both API key and Space ID are required.";
            return;
        }
        try {
            const res = await fetch("/api/v1/anytype/config", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ api_key, space_id }),
            });
            if (!res.ok) {
                const text = await res.text();
                if (status) status.textContent = text || "Failed to save settings.";
                return;
            }
            if (status) status.textContent = "Saved.";
            if (apiKeyEl) apiKeyEl.value = "";
            if (spaceIdEl) spaceIdEl.value = "";
            this.updateAnytypeWarning();
            await this.refreshAll();
            this.closeAnytypeConfigModal();
        } catch (err) {
            console.error("Failed to save Anytype config", err);
            if (status) status.textContent = "Failed to save settings.";
        }
    }

    async refreshAnytypeCache() {
        const status = document.getElementById("anytype-config-status");
        if (status) status.textContent = "Updating…";
        try {
            const res = await fetch("/api/v1/anytype/refresh", { method: "POST" });
            if (!res.ok) {
                const text = await res.text();
                if (status) status.textContent = text || "Failed to update.";
                return;
            }
            if (status) status.textContent = "Updated.";
            await this.refreshAll();
        } catch (err) {
            console.error("Failed to refresh Anytype cache", err);
            if (status) status.textContent = "Failed to update.";
        }
    }

    // ==================== EVENT HANDLERS ====================
    handleFocus() {
        if (!this.wasFocused) {
            this.refreshAll();
        }
        this.wasFocused = true;
    }

    handleBlur() {
        this.wasFocused = false;
    }

    handleKeyDown(event) {
        if (event.key === "Enter") {
            event.preventDefault();
            this.submitTask();
        }
    }

    initHistoryFilters() {
        const setActive = (activeId) => {
            ["week", "month", "year"].forEach((k) => {
                const el = document.getElementById(`history-filter-${k}`);
                if (!el) return;

                const isActive = `history-filter-${k}` === activeId;

                // inactive styles
                el.classList.toggle("text-gray-500", !isActive);
                el.classList.toggle("dark:text-gray-400", !isActive);

                // active styles
                el.classList.toggle("text-primary", isActive);
                el.classList.toggle("dark:text-primary-dark", isActive);
            });
        };

        const bind = (id, days) => {
            const el = document.getElementById(id);
            if (!el) return;

            const t = document.getElementById("dynamic-data-focus");

            el.addEventListener("click", async () => {
                this.historyDays = days;
                setActive(id);
                if (days == 7) {
                    t.textContent = "Week Focus";
                } else if (days == 30) {
                    t.textContent = "Month Focus";
                } else if (days == 365) {
                    t.textContent = "Year Focus";
                }
                await this.renderHistory(days);
            });
        };

        bind("history-filter-week", 7);
        bind("history-filter-month", 30);
        bind("history-filter-year", 365);

        // default
        this.historyDays = 30;
        setActive("history-filter-month");
    }

    // ==================== INITIALIZATION ====================
    setupEventListeners() {
        // Task submission
        document.getElementById("task-submit")?.addEventListener("click", this.submitTask);
        document.getElementById("task-name")?.addEventListener("keydown", this.handleKeyDown);

        // Task double-click
        // In setupEventListeners(), replace the tasks-container click handler with:
        document.getElementById("tasks-container")?.addEventListener("click", async (event) => {
            // Find the clicked button that has data-task-id
            const button = event.target.closest("button[data-task-id]");
            if (!button) return;

            event.stopPropagation();
            const taskId = button.dataset.taskId;
            if (!taskId) return;

            // Find the task in lastTasks
            const task = Array.isArray(this.lastTasks)
                ? this.lastTasks.find((t) => String(t.id) === String(taskId))
                : null;

            if (!task) return;

            const isCurrentlySelected = this.currentTaskId && String(taskId) === String(this.currentTaskId);

            // Clear all markdown containers first
            document.querySelectorAll(".task-markdown-container").forEach((container) => {
                container.remove();
            });

            if (isCurrentlySelected) {
                // Deselect current task
                await this.setCurrentTaskId(null);
                await this.updateServerFocusRules(null);
            } else {
                // Select new task
                await this.setCurrentTaskId(taskId);
                await this.updateServerFocusRules(task);
                // Render markdown for this task
                setTimeout(() => {
                    this.renderTaskMarkdown(task);
                }, 0);
            }

            // Update button states
            this.updateCurrentButtonStates();

            // Send to server
            try {
                const res = await fetch("/api/v1/task/set_current", {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ id: taskId }),
                });

                if (!res.ok) {
                    const text = await res.text();
                    console.warn("Failed to update current task on server:", text);
                }
            } catch (error) {
                console.error("Error updating current task on server:", error);
            }
        });

        // Timer
        document.getElementById("record-toggle")?.addEventListener("click", () => {
            const status = document.getElementById("current-status");
            this.ensureAudioContext();
            if (!this.timerRunning) {
                const seconds = this.parseDurationInput(this.currentDurationValue || "25m");
                this.startTimer(seconds);
                if (status) status.textContent = "Focus running";
            } else {
                this.stopTimer();
                if (status) status.textContent = "Paused";
            }
            this.refreshAll();
        });

        // Category panel
        document.getElementById("category-toggle")?.addEventListener("click", () => {
            const panel = document.getElementById("category-panel");
            if (!panel) return;
            panel.classList.toggle("hidden");
            if (!panel.classList.contains("hidden")) {
                this.hideCategoryMenu();
            } else {
                this.showCategoryMenu();
            }
        });

        document.getElementById("category-panel-close")?.addEventListener("click", () => {
            const panel = document.getElementById("category-panel");
            if (panel) panel.classList.add("hidden");
        });

        // Category input
        const taskCategoryInput = document.getElementById("task-category");
        if (taskCategoryInput) {
            taskCategoryInput.addEventListener("focus", () => this.showCategoryMenu());
            taskCategoryInput.addEventListener("blur", () => setTimeout(() => this.hideCategoryMenu(), 100));
            taskCategoryInput.addEventListener("input", (event) => {
                const value = event.target?.value || "";
                const filtered = this.lastCategories.filter((c) =>
                    String(c.category).toLowerCase().includes(String(value).toLowerCase()),
                );
                this.updateCategorySuggestions(filtered);
            });
        }

        // Meeting app modal and token inputs
        document.querySelector('[data-action="add-meeting-app"]')?.addEventListener("click", (e) => {
            e.preventDefault();
            console.log("add-meeting-app clicked");
            this.openAddMeetingAppModal();
        });

        // Notes app modal and token inputs
        document.querySelector('[data-action="add-note-app"]')?.addEventListener("click", (e) => {
            e.preventDefault();
            console.log("add-note-app clicked");
            this.openAddNoteAppModal();
        });

        document.getElementById("meeting-cancel-button")?.addEventListener("click", (e) => {
            e.preventDefault();
            this.closeAddMeetingAppModal();
        });

        document.getElementById("meeting-add-button")?.addEventListener("click", (e) => {
            e.preventDefault();
            console.log("meeting-add-button clicked");
            this.saveMeetingApp();
        });

        document.getElementById("note-cancel-button")?.addEventListener("click", (e) => {
            e.preventDefault();
            this.closeAddNoteAppModal();
        });

        document.getElementById("note-add-button")?.addEventListener("click", (e) => {
            e.preventDefault();
            console.log("note-add-button clicked");
            this.saveNoteApp();
        });

        // Email app modal and token inputs
        document.querySelector('[data-action="add-email-app"]')?.addEventListener("click", (e) => {
            e.preventDefault();
            console.log("add-email-app clicked");
            this.openAddEmailAppModal();
        });

        document.getElementById("email-cancel-button")?.addEventListener("click", (e) => {
            e.preventDefault();
            this.closeAddEmailAppModal();
        });

        document.getElementById("email-add-button")?.addEventListener("click", (e) => {
            e.preventDefault();
            console.log("email-add-button clicked");
            this.saveEmailApp();
        });

        // Allowed apps/titles
        document.getElementById("allowed-app-add")?.addEventListener("click", () => {
            const input = document.getElementById("allowed-app-input");
            const value = input?.value.trim();
            if (!value) return;
            if (!this.allowedApps.some((a) => a.toLowerCase() === value.toLowerCase())) {
                this.allowedApps.push(value);
                this.renderAllowedLists();
            }
            if (input) input.value = "";
        });

        document.getElementById("allowed-title-add")?.addEventListener("click", () => {
            const input = document.getElementById("allowed-title-input");
            const value = input?.value.trim();
            if (!value) return;
            if (!this.allowedTitles.some((t) => t.toLowerCase() === value.toLowerCase())) {
                this.allowedTitles.push(value);
                this.renderAllowedLists();
            }
            if (input) input.value = "";
        });

        document.getElementById("allowed-app-input")?.addEventListener("keydown", (event) => {
            if (event.key === "Enter") {
                event.preventDefault();
                document.getElementById("allowed-app-add")?.click();
            }
        });

        document.getElementById("allowed-title-input")?.addEventListener("keydown", (event) => {
            if (event.key === "Enter") {
                event.preventDefault();
                document.getElementById("allowed-title-add")?.click();
            }
        });

        // Duration input
        document.getElementById("task-duration")?.addEventListener("input", (event) => {
            if (this.timerRunning) return;
            const nextValue = event.target?.value || "25m";
            this.currentDurationValue = nextValue || "25m";
            const seconds = this.parseDurationInput(this.currentDurationValue);
            this.updateTimerDisplay(seconds);
        });

        // Navigation
        document.querySelectorAll(".nav-link[data-view]").forEach((link) => {
            link.addEventListener("click", (event) => {
                event.preventDefault();
                const view = link.dataset.view || "tasks";
                this.setView(view);
            });
        });

        // History filters
        document.getElementById("history-filter-month")?.addEventListener("click", () => {
            window.historyFilterMode = "month";
            document.getElementById("history-filter-month")?.classList.add("text-primary");
            document.getElementById("history-filter-month")?.classList.remove("text-gray-500", "dark:text-gray-400");
            document.getElementById("history-filter-all")?.classList.remove("text-primary");
            document.getElementById("history-filter-all")?.classList.add("text-gray-500", "dark:text-gray-400");
            this.refreshAll();
        });

        document.getElementById("history-filter-all")?.addEventListener("click", () => {
            window.historyFilterMode = "all";
            document.getElementById("history-filter-all")?.classList.add("text-primary");
            document.getElementById("history-filter-all")?.classList.remove("text-gray-500", "dark:text-gray-400");
            document.getElementById("history-filter-month")?.classList.remove("text-primary");
            document.getElementById("history-filter-month")?.classList.remove("text-gray-500", "dark:text-gray-400");
            document.getElementById("history-filter-month")?.classList.add("text-gray-500", "dark:text-gray-400");
            this.refreshAll();
        });

        // History export
        document.getElementById("history-export")?.addEventListener("click", async () => {
            const history = await this.loadHistory();
            const blob = new Blob([JSON.stringify(history || [], null, 2)], { type: "application/json" });
            const url = URL.createObjectURL(blob);
            const link = document.createElement("a");
            link.href = url;
            link.download = "focus-history.json";
            document.body.appendChild(link);
            link.click();
            link.remove();
            URL.revokeObjectURL(url);
        });

        // Duration modal
        document.getElementById("duration-modal-close")?.addEventListener("click", this.closeDurationModal);
        document.getElementById("duration-modal-cancel")?.addEventListener("click", this.closeDurationModal);
        document.getElementById("duration-modal-save")?.addEventListener("click", this.saveDurationModal);

        document.getElementById("duration-modal")?.addEventListener("click", (event) => {
            if (event.target?.id === "duration-modal") this.closeDurationModal();
        });

        document.getElementById("duration-modal-input")?.addEventListener("keydown", (event) => {
            if (event.key === "Enter") {
                event.preventDefault();
                this.saveDurationModal();
            }
            if (event.key === "Escape") {
                event.preventDefault();
                this.closeDurationModal();
            }
        });

        // Anytype modals
        document.getElementById("anytype-config-open")?.addEventListener("click", this.openAnytypeConfigModal);
        const anytypeConfigButton = document.querySelector("button[data-anytype-config='open']");
        if (anytypeConfigButton && !document.getElementById("anytype-config-open")) {
            anytypeConfigButton.addEventListener("click", this.openAnytypeConfigModal);
        }

        document.getElementById("anytype-config-close")?.addEventListener("click", this.closeAnytypeConfigModal);
        document.getElementById("anytype-config-cancel")?.addEventListener("click", this.closeAnytypeConfigModal);
        document.getElementById("anytype-refresh-open")?.addEventListener("click", this.refreshAnytypeCache);
        document.getElementById("anytype-config-save")?.addEventListener("click", this.saveAnytypeConfig);
        document.getElementById("anytype-auth-challenge")?.addEventListener("click", this.createAnytypeChallenge);
        document.getElementById("anytype-auth-create")?.addEventListener("click", this.createAnytypeApiKey);
        document.getElementById("anytype-spaces-refresh")?.addEventListener("click", this.loadAnytypeSpaces);
        document.getElementById("anytype-space-save")?.addEventListener("click", this.saveAnytypeSpace);

        document.getElementById("anytype-config-modal")?.addEventListener("click", (event) => {
            if (event.target?.id === "anytype-config-modal") this.closeAnytypeConfigModal();
        });

        this.initHistoryFilters();

        const anytypeSpaceIdInput = document.getElementById("anytype-space-id");
        if (anytypeSpaceIdInput) {
            anytypeSpaceIdInput.addEventListener("keydown", (event) => {
                if (event.key === "Enter") {
                    event.preventDefault();
                    this.saveAnytypeConfig();
                }
                if (event.key === "Escape") {
                    event.preventDefault();
                    this.closeAnytypeConfigModal();
                }
            });
        }

        const anytypeApiKeyInput = document.getElementById("anytype-api-key");
        if (anytypeApiKeyInput) {
            anytypeApiKeyInput.addEventListener("keydown", (event) => {
                if (event.key === "Enter") {
                    event.preventDefault();
                    this.saveAnytypeConfig();
                }
                if (event.key === "Escape") {
                    event.preventDefault();
                    this.closeAnytypeConfigModal();
                }
            });
        }

        // Duration presets
        document.querySelectorAll(".duration-preset").forEach((btn) => {
            btn.addEventListener("click", () => {
                const value = btn.getAttribute("data-value") || "";
                const input = document.getElementById("duration-modal-input");
                if (input) input.value = value;
            });
        });

        // Timer display clicks
        document.getElementById("timer-hrs")?.addEventListener("click", this.openDurationModal);
        document.getElementById("timer-min")?.addEventListener("click", this.openDurationModal);
        document.getElementById("timer-sec")?.addEventListener("click", this.openDurationModal);

        // Monitoring toggle
        document.getElementById("monitoring-toggle")?.addEventListener("change", async (e) => {
            const enabled = e.target.checked;
            try {
                const res = await fetch("/api/v1/monitoring", {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ enabled }),
                });
                if (res.ok) {
                    this.monitoringEnabled = enabled;
                } else {
                    e.target.checked = this.monitoringEnabled; // revert
                }
                this.renderCurrentStatus(enabled);
            } catch (err) {
                console.error("Failed to toggle monitoring", err);
                e.target.checked = this.monitoringEnabled; // revert
            }
        });

        // blur
        window.addEventListener("focus", this.handleFocus);
        window.addEventListener("blur", this.handleBlur);

        const meetCategory = document.getElementById("edit-meet-apps");

        const openBtn = document.getElementById("daily-config-open");
        const modal = document.getElementById("daily-config-modal");
        const closeBtn = document.getElementById("daily-config-close");
        const saveBtn = document.getElementById("daily-config-save");
        const cancelBtn = document.getElementById("daily-config-cancel");

        openBtn.onclick = () => modal.classList.remove("hidden");
        closeBtn.onclick = cancelBtn.onclick = () => modal.classList.add("hidden");

        saveBtn.onclick = async () => {
            const data = {
                name: document.getElementById("daily-name").value.trim(),
                appIds: document
                    .getElementById("daily-appids")
                    .value.split(";")
                    .map((s) => s.trim())
                    .filter(Boolean),
                appTitles: document
                    .getElementById("daily-apptitles")
                    .value.split(";")
                    .map((s) => s.trim())
                    .filter(Boolean),
                icon: document.getElementById("daily-icon").value.trim(),
                color: document.getElementById("daily-color").value,
            };

            const res = await fetch("/api/v1/task/recurring_tasks", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(data),
            });
            console.log(res);

            modal.classList.add("hidden");
        };
    }

    startPolling() {
        setInterval(() => {
            const active = this.isPageActive();
            if (!active) {
                this.lastPageActive = false;
                return;
            }

            if (!this.lastPageActive) {
                this.lastPageActive = true;
                if (this.currentView === "tasks") {
                    this.refreshAll();
                }
                return;
            }

            if (this.currentView === "tasks") {
                this.refreshFocusOnly();
            }
        }, 1000); // Keep existing 5-second interval for focus updates
    }

    init() {
        // console.trace();
        this.setActivityIndicator(true);

        // Setup event listeners
        this.setupEventListeners();

        // Setup token inputs for meeting app autocompletion
        this.setupTokenInput(document.getElementById("meetingAppIds"));
        this.setupTokenInput(document.getElementById("meetingAppTitles"));
        // Notes token inputs
        this.setupTokenInput(document.getElementById("noteAppIds"));
        this.setupTokenInput(document.getElementById("noteAppTitles"));
        // Email token inputs
        this.setupTokenInput(document.getElementById("emailAppIds"));
        this.setupTokenInput(document.getElementById("emailAppTitles"));

        // Set initial view
        this.setView("tasks");

        // Refresh everything
        this.refreshEverything();

        // Load settings
        this.loadSettings();

        // Start polling
        this.startPolling();

        // refreshAll
        this.refreshAll();
    }
}

// Initialize the app when the window loads
window.onload = function () {
    const app = new FocusApp();
    app.init();
    window.focusApp = app;
};
