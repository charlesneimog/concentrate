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

        // Set default filter mode
        window.historyFilterMode = "month";

        // Initialize methods that need to be bound
        this.refreshFocusOnly = this.refreshFocusOnly.bind(this);
        this.refreshAll = this.refreshAll.bind(this);
        this.setView = this.setView.bind(this);
        this.handleKeyDown = this.handleKeyDown.bind(this);
        this.handlePageVisibility = this.handlePageVisibility.bind(this);
        this.handlePageFocus = this.handlePageFocus.bind(this);

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

        console.log("FocusApp initialized");
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
        return value
            .split(",")
            .map((s) => s.trim())
            .filter(Boolean);
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
            const res = await fetch("/anytype/tasks", { cache: "no-store" });
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
        const res = await fetch("/current", { cache: "no-store" });
        if (!res.ok) return null;
        const cur = await res.json();
        return Object.keys(cur || {}).length ? cur : null;
    }

    async loadEvents() {
        const res = await fetch("/events", { cache: "no-store" });
        if (!res.ok) return [];
        const events = await res.json();
        if (!Array.isArray(events)) return [];
        return events;
    }

    async loadCategories() {
        const res = await fetch("/categories", { cache: "no-store" });
        if (!res.ok) return [];
        const categories = await res.json();
        if (!Array.isArray(categories)) return [];
        return categories.filter((c) => c && c.category);
    }

    async loadHistory() {
        const res = await fetch("/history", { cache: "no-store" });
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
            await fetch("/focus/rules", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(payload),
            });
        } catch (err) {
            console.warn("Failed to update focus rules", err);
        }
    }

    // ==================== STATE MANAGEMENT ====================
    setCurrentTaskId(taskId) {
        this.currentTaskId = taskId ? String(taskId) : null;
        if (this.currentTaskId) {
            localStorage.setItem("currentTaskId", String(this.currentTaskId));
        } else {
            localStorage.removeItem("currentTaskId");
        }
        this.renderTasks(this.lastTasks);
        this.renderCurrentTask(this.lastTasks);
        this.updateFocusWarning(null, this.lastTasks);
        const task = Array.isArray(this.lastTasks)
            ? this.lastTasks.find((t) => String(t.id) === String(this.currentTaskId))
            : null;
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

    groupTasksByCategory(tasks) {
        const grouped = new Map();
        (Array.isArray(tasks) ? tasks : []).forEach((task) => {
            const key = (task.category || "Uncategorized").trim() || "Uncategorized";
            if (!grouped.has(key)) grouped.set(key, []);
            grouped.get(key).push(task);
        });
        return grouped;
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
            row.className = "w-full text-left px-3 py-2 text-sm hover:bg-gray-50";
            row.textContent = c.category;
            if (c.category === "(no categories)") {
                row.disabled = true;
                row.className = "w-full text-left px-3 py-2 text-sm text-text-muted";
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
                chip.className = "px-2 py-1 rounded-full text-xs bg-gray-100 shadow-sm text-gray-700";
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
                chip.className = "px-2 py-1 rounded-full text-xs bg-gray-100 shadow-sm text-gray-700";
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

        if (!tasks.length) {
            const empty = document.createElement("div");
            empty.className = "text-sm text-gray-400";
            empty.textContent = this.anytypeError ? this.anytypeError : "No tasks yet.";
            container.appendChild(empty);
            return;
        }

        const grouped = this.groupTasksByCategory(tasks);
        for (const [category, items] of grouped.entries()) {
            const section = document.createElement("div");
            section.className = "flex flex-col gap-2";

            const title = document.createElement("h2");
            title.className = "text-xs font-semibold uppercase tracking-wider text-gray-500 pl-1";
            title.textContent = category;
            section.appendChild(title);

            const list = document.createElement("ul");
            list.className = "space-y-2";

            items.forEach((task) => {
                const done = !!task.done;
                const isCurrent = this.currentTaskId && String(task.id) === String(this.currentTaskId);
                const li = document.createElement("li");
                li.className = "flex flex-col gap-1 bg-white shadow-sm rounded-lg px-3 py-2";
                li.dataset.taskId = task.id; // Use data-task-id instead of id

                const row = document.createElement("div");
                row.className = "flex items-center gap-2";

                const mark = document.createElement("span");
                mark.className = `font-mono text-xs ${done ? "text-emerald-600" : "text-gray-400"}`;
                mark.textContent = done ? "[x]" : "[ ]";

                const text = document.createElement("span");
                text.className = `text-sm font-medium ${done ? "text-gray-400 line-through" : "text-gray-800"}`;
                text.textContent = task.title || "(task)";

                const spacer = document.createElement("span");
                spacer.className = "flex-1";

                const currentBtn = document.createElement("button");
                currentBtn.type = "button";
                currentBtn.className = `h-7 w-7 rounded shadow-sm ${isCurrent ? "bg-emerald-500 border-emerald-500 text-white" : "border-gray-200 text-gray-400 hover:text-emerald-500"} flex items-center justify-center transition-all`;
                currentBtn.title = isCurrent ? "Current task" : "Set as current";
                currentBtn.innerHTML = `<span class="material-symbols-outlined text-[16px]">${isCurrent ? "radio_button_checked" : "radio_button_unchecked"}</span>`;

                // Current button click handler
                currentBtn.addEventListener("click", (event) => {
                    event.stopPropagation();
                    const isCurrentlySelected = this.currentTaskId && String(task.id) === String(this.currentTaskId);

                    // Clear all markdown containers first
                    document.querySelectorAll(".task-markdown-container").forEach((container) => {
                        container.remove();
                    });

                    if (isCurrentlySelected) {
                        this.setCurrentTaskId(null);
                        this.updateServerFocusRules(null);
                    } else {
                        this.setCurrentTaskId(task.id);
                        this.updateServerFocusRules(task);
                        // Render markdown for this task
                        setTimeout(() => {
                            this.renderTaskMarkdown(task);
                        }, 0);
                    }

                    // Update button states without re-rendering everything
                    this.updateCurrentButtonStates();
                });

                currentBtn.dataset.taskId = task.id;
                currentBtn.dataset.taskCurrent = isCurrent ? "true" : "false";

                row.appendChild(mark);
                row.appendChild(text);
                row.appendChild(spacer);
                row.appendChild(currentBtn);

                const debug = document.createElement("div");
                debug.className = "text-[10px] text-gray-500 flex flex-wrap gap-2";
                const allowedApps = this.normalizeAllowList(task.allowed_app_ids);
                const allowedTitles = this.normalizeAllowList(task.allowed_titles);
                const appLabel = allowedApps.length ? allowedApps.join(", ") : "Any";
                const titleLabel = allowedTitles.length ? allowedTitles.join(", ") : "Any";

                const allowedChip = document.createElement("span");
                allowedChip.className = "px-2 py-0.5 rounded bg-gray-50 border border-gray-100";
                allowedChip.textContent = `Allowed apps: ${appLabel}`;

                const titleChip = document.createElement("span");
                titleChip.className = "px-2 py-0.5 rounded bg-gray-50 border border-gray-100";
                titleChip.textContent = `Allowed titles: ${titleLabel}`;

                const focusChip = document.createElement("span");
                const focusAllowed = this.isFocusAllowed(this.lastCurrentFocus, task);
                focusChip.className = `px-2 py-0.5 rounded border ${focusAllowed ? "bg-emerald-50 border-emerald-200 text-emerald-700" : "bg-rose-50 border-rose-200 text-rose-700"}`;
                focusChip.textContent = focusAllowed ? "Current app: allowed" : "Current app: NOT allowed";

                debug.appendChild(allowedChip);
                debug.appendChild(titleChip);
                if (this.lastCurrentFocus) debug.appendChild(focusChip);

                li.appendChild(row);
                li.appendChild(debug);

                // Only render markdown if this is the current task
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
        markdownContainer.className = "task-markdown-container mt-3 pt-3 border-t border-gray-100";
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
            return `<code class="bg-gray-100 px-1 py-0.5 rounded text-sm font-mono">${escapedCode}</code>`;
        });

        // Then process multiline code blocks (triple backticks)
        html = html.replace(/```([\s\S]*?)```/g, (match, code) => {
            // Unescape any remaining escaped characters in code blocks
            const unescapedCode = code.replace(/\\_/g, "_").replace(/\\\*/g, "*").replace(/\\`/g, "`");
            const escapedCode = this.escapeHtml(unescapedCode.trim());
            return `<pre class="bg-gray-50 p-3 rounded-lg overflow-x-auto my-2"><code class="text-sm font-mono">${escapedCode}</code></pre>`;
        });

        // Process headers (h1, h2, h3)
        html = html.replace(/^### (.*$)/gm, (match, content) => {
            // Don't escape the entire content as it may contain HTML from code blocks
            // Just escape any remaining plain text
            const processedContent = content.replace(
                /(?<!<\/?code[^>]*>)(?<!<\/?strong[^>]*>)(?<!<\/?em[^>]*>)[^<>]+/g,
                (text) => this.escapeHtml(text),
            );
            return `<h3 class="text-sm font-semibold text-gray-800 mt-3 mb-1">${processedContent}</h3>`;
        });

        html = html.replace(/^## (.*$)/gm, (match, content) => {
            const processedContent = content.replace(
                /(?<!<\/?code[^>]*>)(?<!<\/?strong[^>]*>)(?<!<\/?em[^>]*>)[^<>]+/g,
                (text) => this.escapeHtml(text),
            );
            return `<h2 class="text-base font-bold text-gray-900 mt-4 mb-2">${processedContent}</h2>`;
        });

        html = html.replace(/^# (.*$)/gm, (match, content) => {
            const processedContent = content.replace(
                /(?<!<\/?code[^>]*>)(?<!<\/?strong[^>]*>)(?<!<\/?em[^>]*>)[^<>]+/g,
                (text) => this.escapeHtml(text),
            );
            return `<h1 class="text-lg font-bold text-gray-900 mt-5 mb-3">${processedContent}</h1>`;
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

            return `<li class="ml-4 pl-1 text-sm text-gray-700 flex items-start">${checkboxHtml}<span class="flex-1">${contentHtml}</span></li>`;
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
                        processedLines.push(`<p class="text-sm text-gray-700 my-2">${escapedText}</p>`);
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
                        processedLines.push(`<p class="text-sm text-gray-700 my-2">${escapedText}</p>`);
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
                processedLines.push(`<p class="text-sm text-gray-700 my-2">${escapedText}</p>`);
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
        if (!current) {
            status.textContent = "Idle";
            return;
        }
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
        const key = `${task.id}:${current.app_id || ""}:${current.title || ""}`;
    }

    renderStats(history, events, tasks) {
        const pie = document.getElementById("stats-pie");
        const legend = document.getElementById("stats-legend");
        const totalEl = document.getElementById("stats-total");
        const sessionsEl = document.getElementById("stats-sessions");
        const streakEl = document.getElementById("stats-streak");
        if (!pie || !legend || !totalEl || !sessionsEl) return;

        const totals = new Map();
        const todayKey = this.toLocalDateKey(new Date());
        (Array.isArray(events) ? events : []).forEach((item) => {
            const ts = this.normalizeTimestamp(item?.end_time || item?.start_time || 0);
            if (!ts) return;
            const keyDate = this.toLocalDateKey(new Date(ts * 1000));
            if (keyDate !== todayKey) return;
            const key = item.category || item.app_id || "Others";
            const prev = totals.get(key) || 0;
            totals.set(key, prev + (Number(item.duration) || 0));
        });

        const rawEntries = Array.from(totals.entries()).filter(([, v]) => v > 0);
        const total = rawEntries.reduce((sum, [, v]) => sum + v, 0);

        const todayTotal = (Array.isArray(events) ? events : []).reduce((sum, event) => {
            const ts = this.normalizeTimestamp(event?.end_time || event?.start_time || 0);
            if (!ts) return sum;
            const keyDate = this.toLocalDateKey(new Date(ts * 1000));
            if (keyDate !== todayKey) return sum;
            return sum + (Number(event?.duration) || 0);
        }, 0);

        totalEl.textContent = this.fmtDuration(todayTotal);
        const completed = Array.isArray(tasks) ? tasks.filter((t) => t.done).length : 0;
        sessionsEl.textContent = String(completed);

        if (streakEl) {
            const goalSeconds = Math.round(4.5 * 3600);
            const byDay = new Map();
            events.forEach((e) => {
                const ts = Number(e.end_time || e.start_time || 0);
                if (!ts) return;
                const day = new Date(ts * 1000).toISOString().slice(0, 10);
                const prev = byDay.get(day) || 0;
                byDay.set(day, prev + (Number(e.duration) || 0));
            });
            const days = Array.from(byDay.keys()).sort();
            let streak = 0;
            for (let i = days.length - 1; i >= 0; i -= 1) {
                const day = days[i];
                const totalDay = byDay.get(day) || 0;
                if (totalDay >= goalSeconds) {
                    streak += 1;
                } else {
                    break;
                }
            }
            streakEl.innerHTML = `${streak} <span class="text-xs font-normal text-gray-400">days</span>`;
        }

        legend.innerHTML = "";
        if (!rawEntries.length || total <= 0) {
            pie.style.background = "conic-gradient(#94a3b8 0% 100%)";
            legend.innerHTML = `
                <div class="flex items-center justify-between text-sm">
                    <div class="flex items-center gap-2">
                        <span class="w-3 h-3 rounded-full bg-slate-400 shadow-sm"></span>
                        <span class="text-gray-600">No data</span>
                    </div>
                    <span class="font-medium text-gray-900">100%</span>
                </div>
            `;
            return;
        }

        const filtered = [];
        let othersTotal = 0;
        rawEntries.forEach(([label, value]) => {
            if (total > 0 && value / total < 0.02) {
                othersTotal += value;
            } else {
                filtered.push([label, value]);
            }
        });
        if (othersTotal > 0) {
            filtered.push(["Others", othersTotal]);
        }

        const colors = [
            "#2563EB",
            "#A855F7",
            "#F97316",
            "#10b981",
            "#f59e0b",
            "#06b6d4",
            "#ef4444",
            "#3b82f6",
            "#94a3b8",
        ];

        let offset = 0;
        const slices = filtered.map(([label, value], idx) => {
            const percent = (value / total) * 100;
            const color = colors[idx % colors.length];
            const slice = `${color} ${offset}% ${offset + percent}%`;
            offset += percent;
            const row = document.createElement("div");
            row.className = "flex items-center justify-between text-sm";
            row.innerHTML = `
                <div class="flex items-center gap-2">
                    <span class="w-3 h-3 rounded-full" style="background:${color}"></span>
                    <span class="text-gray-600">${this.escapeHtml(label)}</span>
                </div>
                <span class="font-medium text-gray-900">${Math.round(percent)}%</span>
            `;
            legend.appendChild(row);
            return slice;
        });

        pie.style.background = `conic-gradient(${slices.join(", ")})`;
    }

    renderHistory(history, events, tasks) {
        const list = document.getElementById("history-list");
        if (!list) return;

        const items = this.normalizeHistoryItems(history, events).sort((a, b) => (b.start || 0) - (a.start || 0));
        const filterMode = window.historyFilterMode || "month";
        const now = new Date();
        const startOfMonth = new Date(now.getFullYear(), now.getMonth(), 1);
        const filtered = items.filter((item) => {
            if (filterMode === "all") return true;
            const startDate = item.start ? new Date(item.start * 1000) : null;
            return startDate ? startDate >= startOfMonth : false;
        });

        const grouped = new Map();
        filtered.forEach((item) => {
            const date = new Date((item.start || item.end || 0) * 1000);
            const key = this.toLocalDateKey(date);
            if (!grouped.has(key)) grouped.set(key, []);
            grouped.get(key).push(item);
        });

        const keys = Array.from(grouped.keys()).sort((a, b) => (a < b ? 1 : -1));
        list.innerHTML = "";

        if (!keys.length) {
            const empty = document.createElement("div");
            empty.className = "text-sm text-gray-400";
            empty.textContent = "No history available yet.";
            list.appendChild(empty);
        }

        keys.forEach((key, index) => {
            const entries = grouped.get(key) || [];
            const dayDate = new Date(key + "T00:00:00");
            const start = entries.reduce((min, item) => Math.min(min, item.start || min), entries[0]?.start || 0);
            const end = entries.reduce((max, item) => Math.max(max, item.end || max), entries[0]?.end || 0);
            const totalDuration = entries.reduce((sum, item) => sum + (item.duration || 0), 0);
            const opacity = Math.max(0.7, 1 - index * 0.05);

            const card = document.createElement("div");
            card.className =
                "bg-white shadow-sm rounded-xl overflow-hidden shadow-subtle hover:border-primary/30 transition-all";
            card.style.opacity = String(opacity);

            const header = document.createElement("div");
            header.className = "bg-gray-50 px-5 py-3 shadow-sm flex justify-between items-center";
            header.innerHTML = `
                <div class="flex items-center gap-4">
                    <div class="flex flex-col">
                        <span class="text-sm font-bold text-text-main">${this.escapeHtml(this.formatDayLabel(dayDate))}</span>
                        <span class="text-xs text-text-muted">${this.escapeHtml(this.formatTimeRange(new Date(start * 1000), new Date(end * 1000)))}</span>
                    </div>
                    <div class="h-8 w-px bg-gray-200"></div>
                    <div class="flex items-center gap-1.5">
                        <span class="material-symbols-outlined text-primary text-lg">timer</span>
                        <span class="text-sm font-bold text-primary">${this.escapeHtml(this.fmtDuration(totalDuration))}</span>
                    </div>
                </div>
                <button class="text-gray-400 hover:text-text-main" type="button"><span class="material-symbols-outlined">more_horiz</span></button>
            `;

            const body = document.createElement("div");
            body.className = "p-5";
            const grid = document.createElement("div");
            grid.className = "grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-3";
            entries.forEach((item) => {
                const row = document.createElement("div");
                const label = item.task || item.title || item.app_id || "Session";
                const displayLabel = this.truncateText(label, 32);
                const badgeLabel = item.app_id || "Unknown app";
                const badgeStyle = this.appIdBadgeStyle(badgeLabel);
                row.className = "flex items-center justify-between gap-3 p-3 rounded-lg shadow-sm bg-white";
                row.innerHTML = `
                    <div class="flex items-center gap-3 min-w-0 flex-1">
                        <span class="material-symbols-outlined text-emerald-500 text-sm">check_circle</span>
                        <span class="text-sm text-text-main truncate" title="${this.escapeHtml(label)}">${this.escapeHtml(displayLabel)}</span>
                    </div>
                    <span class="px-2 py-0.5 rounded text-[10px] font-bold border max-w-[160px] truncate shrink-0" title="${this.escapeHtml(badgeLabel)}" style="background:${badgeStyle.background};color:${badgeStyle.color};border-color:${badgeStyle.borderColor}">${this.escapeHtml(this.truncateText(badgeLabel, 24))}</span>
                `;
                grid.appendChild(row);
            });
            body.appendChild(grid);

            card.appendChild(header);
            card.appendChild(body);
            list.appendChild(card);
        });

        const weeklyTotalEl = document.getElementById("history-weekly-total");
        const avgSessionEl = document.getElementById("history-avg-session");
        const tasksCompletedEl = document.getElementById("history-tasks-completed");
        const tasksTodayEl = document.getElementById("history-tasks-today");

        const weekStart = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 6);
        const weeklyTotal = items
            .filter((item) => item.start && new Date(item.start * 1000) >= weekStart)
            .reduce((sum, item) => sum + (item.duration || 0), 0);
        if (weeklyTotalEl) weeklyTotalEl.textContent = this.fmtDuration(weeklyTotal);

        const avg = items.length ? items.reduce((sum, item) => sum + (item.duration || 0), 0) / items.length : 0;
        if (avgSessionEl) avgSessionEl.textContent = this.fmtDuration(avg);

        const completedTasks = Array.isArray(tasks) ? tasks.filter((t) => t.done).length : 0;
        if (tasksCompletedEl) tasksCompletedEl.textContent = String(completedTasks);

        const todayKey = this.toLocalDateKey(now);
        const todayEntries = (grouped.get(todayKey) || []).length;
        if (tasksTodayEl) tasksTodayEl.textContent = `${todayEntries} today`;

        this.renderHistoryStats(filtered);
        this.renderHistoryHeatmap(filtered);
    }

    renderHistoryStats(items) {
        const pie = document.getElementById("history-pie");
        const legend = document.getElementById("history-legend");
        const monthLabel = document.getElementById("history-month-label");
        const goalText = document.getElementById("history-goal-text");
        const goalProgress = document.getElementById("history-goal-progress");
        if (!pie || !legend) return;

        const totals = new Map();
        items.forEach((item) => {
            const key = item.app_id || "Others";
            const prev = totals.get(key) || 0;
            totals.set(key, prev + (item.duration || 0));
        });
        const entries = Array.from(totals.entries())
            .filter(([, v]) => v > 0)
            .sort((a, b) => b[1] - a[1]);
        const total = entries.reduce((sum, [, v]) => sum + v, 0);
        legend.innerHTML = "";

        if (!entries.length || total <= 0) {
            pie.style.background = "conic-gradient(#94a3b8 0% 100%)";
            legend.innerHTML = `
                <div class="flex items-center justify-between text-sm">
                    <div class="flex items-center gap-2">
                        <span class="w-3 h-3 rounded-full bg-slate-400 shadow-sm"></span>
                        <span class="text-gray-600">No data</span>
                    </div>
                    <span class="font-medium text-gray-900">100%</span>
                </div>
            `;
        } else {
            const colors = [
                "#2563EB",
                "#A855F7",
                "#F97316",
                "#10b981",
                "#f59e0b",
                "#06b6d4",
                "#ef4444",
                "#3b82f6",
                "#94a3b8",
            ];
            let offset = 0;
            const slices = entries.map(([label, value], idx) => {
                const percent = (value / total) * 100;
                const color = colors[idx % colors.length];
                const slice = `${color} ${offset}% ${offset + percent}%`;
                offset += percent;
                const row = document.createElement("div");
                row.className = "flex items-center justify-between text-sm";
                row.innerHTML = `
                    <div class="flex items-center gap-2">
                        <span class="w-3 h-3 rounded-full" style="background:${color}"></span>
                        <span class="text-gray-600">${this.escapeHtml(label)}</span>
                    </div>
                    <span class="font-medium text-gray-900">${Math.round(percent)}%</span>
                `;
                legend.appendChild(row);
                return slice;
            });
            pie.style.background = `conic-gradient(${slices.join(", ")})`;
        }

        if (monthLabel) {
            const now = new Date();
            monthLabel.textContent = now.toLocaleDateString(undefined, { month: "short", year: "numeric" });
        }

        if (goalText && goalProgress) {
            const monthStart = new Date(new Date().getFullYear(), new Date().getMonth(), 1);
            const monthSeconds = items
                .filter((item) => item.start && new Date(item.start * 1000) >= monthStart)
                .reduce((sum, item) => sum + (item.duration || 0), 0);
            const goalSeconds = 120 * 3600;
            const pct = Math.min(100, Math.round((monthSeconds / goalSeconds) * 100));
            goalText.textContent = `${this.fmtDuration(monthSeconds)} focused this month. On track for your ${Math.round(goalSeconds / 3600)}h goal.`;
            goalProgress.style.width = `${pct}%`;
        }
    }

    renderHistoryHeatmap(items) {
        const container = document.getElementById("history-heatmap");
        const startLabel = document.getElementById("history-heatmap-start");
        const endLabel = document.getElementById("history-heatmap-end");
        if (!container) return;

        const days = 28;
        const today = new Date();
        const start = new Date(today.getFullYear(), today.getMonth(), today.getDate() - (days - 1));
        const totalsByDay = new Map();
        items.forEach((item) => {
            if (!item.start) return;
            const date = new Date(item.start * 1000);
            const key = this.toLocalDateKey(date);
            const prev = totalsByDay.get(key) || 0;
            totalsByDay.set(key, prev + (item.duration || 0));
        });

        container.innerHTML = "";
        let max = 0;
        totalsByDay.forEach((value) => {
            if (value > max) max = value;
        });

        for (let i = 0; i < days; i += 1) {
            const d = new Date(start.getFullYear(), start.getMonth(), start.getDate() + i);
            const key = this.toLocalDateKey(d);
            const total = totalsByDay.get(key) || 0;
            let level = "heatmap-empty";
            if (max > 0) {
                const ratio = total / max;
                if (ratio >= 0.66) level = "heatmap-high";
                else if (ratio >= 0.33) level = "heatmap-mid";
                else if (ratio > 0) level = "heatmap-low";
            }
            const cell = document.createElement("div");
            cell.className = `heatmap-cell ${level}`;
            container.appendChild(cell);
        }

        if (startLabel)
            startLabel.textContent = start.toLocaleDateString(undefined, { month: "short", day: "numeric" });
        if (endLabel) endLabel.textContent = today.toLocaleDateString(undefined, { month: "short", day: "numeric" });
    }

    // ==================== VIEW MANAGEMENT ====================

    setView(view) {
        this.currentView = view;
        const tasksView = document.getElementById("view-tasks");
        const historyView = document.getElementById("view-history");
        const tasksAside = document.getElementById("tasks-aside");
        const historyAside = document.getElementById("history-aside");
        if (tasksView) tasksView.classList.toggle("hidden", view !== "tasks");
        if (historyView) historyView.classList.toggle("hidden", view !== "history");
        if (tasksAside) tasksAside.classList.toggle("lg:flex", view === "tasks");
        if (historyAside) historyAside.classList.toggle("lg:flex", view === "history");

        document.querySelectorAll(".nav-link[data-view]").forEach((link) => {
            const isActive = link.dataset.view === view;
            link.classList.toggle("bg-primary/10", isActive);
            link.classList.toggle("text-primary", isActive);
            link.classList.toggle("font-semibold", isActive);
            link.classList.toggle("text-text-muted", !isActive);
            link.classList.toggle("hover:bg-white", !isActive);
            link.classList.toggle("hover:text-text-main", !isActive);
            link.classList.toggle("hover:shadow-subtle", !isActive);
        });

        // this.refreshAll();
    }

    isPageActive() {
        return document.visibilityState === "visible";
    }

    async refreshFocusOnly() {
        if (!this.isPageActive()) return;
        try {
            const current = await this.loadCurrent();
            this.lastCurrentFocus = current;
            this.renderCurrentStatus(current);
            this.updateFocusWarning(current, this.lastTasks);
        } catch (err) {
            console.error("Failed to load current focus", err);
        }
    }

    async refreshAll() {
        try {
            if (this.currentView === "history") {
                const [tasks, current, categories, history, events] = await Promise.all([
                    this.loadTasks(),
                    this.loadCurrent(),
                    this.loadCategories(),
                    this.loadHistory(),
                    this.loadEvents(),
                ]);

                this.lastTasks = Array.isArray(tasks) ? tasks : [];
                this.lastCurrentFocus = current;
                this.renderTasks(tasks);
                this.updateAnytypeWarning();
                this.renderCurrentStatus(current);
                this.renderCurrentTask(tasks);
                this.updateFocusWarning(current, tasks);
                this.updateCategorySuggestions(categories);
                this.renderStats(history, events, tasks);
                this.renderHistory(history, events, tasks);
                return;
            }

            const [tasks, current, categories] = await Promise.all([
                this.loadTasks(),
                this.loadCurrent(),
                this.loadCategories(),
            ]);
            this.lastTasks = Array.isArray(tasks) ? tasks : [];
            this.lastCurrentFocus = current;
            this.renderTasks(tasks);
            this.updateAnytypeWarning();
            this.renderCurrentStatus(current);
            this.renderCurrentTask(tasks);
            this.updateFocusWarning(current, tasks);
            this.updateCategorySuggestions(categories);
        } catch (err) {
            console.error("Failed to load data", err);
        }
    }

    async refreshEverything() {
        try {
            const tasks = await this.loadTasks();
            const current = await this.loadCurrent();
            const categories = await this.loadCategories();
            const history = await this.loadHistory();
            const events = await this.loadEvents();

            this.lastTasks = Array.isArray(tasks) ? tasks : [];
            this.lastCurrentFocus = current;
            this.renderTasks(tasks);
            this.updateAnytypeWarning();
            this.renderCurrentStatus(current);
            this.renderCurrentTask(tasks);
            this.updateFocusWarning(current, tasks);
            this.updateCategorySuggestions(categories);
            this.renderStats(history, events, tasks);
            this.renderHistory(history, events, tasks);
        } catch (err) {
            console.error("Failed to load data", err);
        }
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

        const res = await fetch("/tasks", {
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
            const res = await fetch("/anytype/auth/challenges", {
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
            const res = await fetch("/anytype/auth/api_keys", {
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
            const res = await fetch("/anytype/spaces", { cache: "no-store" });
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
            const res = await fetch("/anytype/space", {
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
            const res = await fetch("/anytype/config", {
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
            const res = await fetch("/anytype/refresh", { method: "POST" });
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

    handleKeyDown(event) {
        if (event.key === "Enter") {
            event.preventDefault();
            this.submitTask();
        }
    }

    handlePageVisibility() {
        const active = this.isPageActive();
        if (active && !this.lastPageActive) {
            this.lastPageActive = true;
            if (this.currentView === "tasks") {
                this.refreshAll();
            }
        } else if (!active) {
            this.lastPageActive = false;
        }
    }

    handlePageFocus() {
        if (!this.lastPageActive && this.isPageActive()) {
            this.lastPageActive = true;
            if (this.currentView === "tasks") {
                this.refreshAll();
            }
        }
    }

    // ==================== INITIALIZATION ====================

    setupEventListeners() {
        // Task submission
        document.getElementById("task-submit")?.addEventListener("click", this.submitTask);
        document.getElementById("task-name")?.addEventListener("keydown", this.handleKeyDown);

        // Task container events
        document.getElementById("tasks-container")?.addEventListener("click", async (event) => {
            const noteTarget = event.target?.closest("button[data-task-note]");
            const currentTarget = event.target?.closest("button[data-task-current]");
            if (currentTarget) {
                const taskId = currentTarget.dataset.taskId || "";
                if (!taskId) return;
                this.setCurrentTaskId(taskId);
                const res = await fetch("/task/set_current", {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({ id: taskId }),
                });
                if (!res.ok) {
                    const text = await res.text();
                    alert("Failed to update task: " + text);
                    return;
                }
                await this.refreshAll();
                return;
            }
        });

        window.addEventListener("focus", () => {
            this.handlePageFocus();
        });

        // Task double-click
        document.getElementById("tasks-container")?.addEventListener("dblclick", async (event) => {
            const row = event.target?.closest(".task-row");
            if (!row) return;
            const taskId = Number(row.dataset.taskId || 0);
            if (!taskId) return;

            const current = await this.loadCurrent();
            const defaultApp = current?.app_id || "";
            const defaultTitle = current?.title || "";

            const appId = prompt("Allowed App ID (leave blank to keep):", defaultApp);
            if (appId == null) return;
            const title = prompt("Allowed Title keyword (leave blank to keep):", defaultTitle);
            if (title == null) return;

            const payload = { id: taskId };
            if (appId.trim()) payload.allowed_app_ids = [appId.trim()];
            if (title.trim()) payload.allowed_titles = [title.trim()];

            const res = await fetch("/tasks/update", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(payload),
            });
            if (!res.ok) {
                const text = await res.text();
                alert("Failed to update task: " + text);
                return;
            }
            await this.refreshAll();
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
            document.getElementById("history-filter-month")?.classList.remove("text-text-muted");
            document.getElementById("history-filter-all")?.classList.remove("text-primary");
            document.getElementById("history-filter-all")?.classList.add("text-text-muted");
            this.refreshAll();
        });

        document.getElementById("history-filter-all")?.addEventListener("click", () => {
            window.historyFilterMode = "all";
            document.getElementById("history-filter-all")?.classList.add("text-primary");
            document.getElementById("history-filter-all")?.classList.remove("text-text-muted");
            document.getElementById("history-filter-month")?.classList.remove("text-primary");
            document.getElementById("history-filter-month")?.classList.remove("text-text-muted");
            document.getElementById("history-filter-month")?.classList.add("text-text-muted");
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

        // Page visibility and focus
        window.addEventListener("focus", this.handlePageFocus);
        document.addEventListener("visibilitychange", this.handlePageVisibility);
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
        console.log("FocusApp initializing...");

        // Setup activity indicator
        this.setActivityIndicator(true);

        // Setup event listeners
        this.setupEventListeners();

        // Set initial view
        this.setView("tasks");

        // Refresh everything
        this.refreshEverything();

        // Start polling
        this.startPolling();

        // refreshAll
        this.refreshAll();

        console.log("FocusApp initialized successfully");
    }
}

// Initialize the app when the window loads
window.onload = function () {
    const app = new FocusApp();
    app.init();
    window.focusApp = app;
};
