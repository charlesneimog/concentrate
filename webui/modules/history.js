import * as API from "../api/client.js";
const GOAL_HOURS_PER_DAY = 6;

export class HistoryManager {
    constructor(app) {
        this.app = app;
        const methods = [
            "historyManagerInitHistoryFilters",
            "renderHistoryCategoryStats",
            "renderHistory",
            "updateDailyFocus",
            "updateHydrationSummary",
            "updateMonitoringSummary",
            "updateDailyActivities",
            "excludeDailyActivity",
        ];
        methods.forEach((name) => {
            app[name] = this[name].bind(app);
        });
    }

    historyManagerInitHistoryFilters() {
        const setActive = (activeId) => {
            ["day", "week", "month", "year"].forEach((k) => {
                const el = document.getElementById(`history-filter-${k}`);
                if (!el) return;

                const isActive = `history-filter-${k}` === activeId;

                el.classList.toggle("text-gray-500", !isActive);
                el.classList.toggle("dark:text-gray-400", !isActive);

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
                if (t) {
                    if (days == 1) {
                        t.textContent = "Day Focus";
                    } else if (days == 7) {
                        t.textContent = "Week Focus";
                    } else if (days == 30) {
                        t.textContent = "Month Focus";
                    } else if (days == 365) {
                        t.textContent = "Year Focus";
                    }
                }
                await this.renderHistory(days);
            });
        };

        bind("history-filter-day", 1);
        bind("history-filter-week", 7);
        bind("history-filter-month", 30);
        bind("history-filter-year", 365);

        this.historyDays = 30;
        setActive("history-filter-month");
    }

    async renderHistoryCategoryStats(days) {
        const timeEl = document.getElementById("history-category-time");
        const focusEl = document.getElementById("history-category-focus");
        if (!timeEl || !focusEl) return;

        // Prevent blink: keep existing DOM until the new content is ready,
        // then swap while faded out.
        const token = (this._historyCategoryStatsToken ?? 0) + 1;
        this._historyCategoryStatsToken = token;

        const nextFrame = () => new Promise((r) => requestAnimationFrame(() => r()));

        const startLoading = (el) => {
            el.style.willChange = "opacity";
            el.style.transition = "opacity 140ms ease";
            el.style.opacity = "0.55";
            if (!el.childElementCount) {
                el.innerHTML = `<div class="text-xs text-gray-400 dark:text-gray-300">Loading...</div>`;
            }
        };

        const swapWithFade = async (el, fragment) => {
            el.style.willChange = "opacity";
            el.style.transition = "opacity 350ms ease";
            el.style.opacity = "0";
            await nextFrame();
            if (this._historyCategoryStatsToken !== token) return;
            el.innerHTML = "";
            el.appendChild(fragment);
            // Ensure the browser sees the new content at opacity 0 before fading in.
            await nextFrame();
            if (this._historyCategoryStatsToken !== token) return;
            el.style.opacity = "1";
        };

        startLoading(timeEl);
        startLoading(focusEl);

        try {
            if (typeof this.ensureCategoryColors === "function") {
                await this.ensureCategoryColors();
            }

            const [timeRes, focusRes] = await Promise.all([
                fetch(`/api/v1/history/category-time?days=${days}`, { cache: "no-store" }),
                fetch(`/api/v1/history/category-focus?days=${days}`, { cache: "no-store" }),
            ]);

            const timeFrag = document.createDocumentFragment();
            if (timeRes.ok) {
                const timeRows = await timeRes.json();
                const rows = Array.isArray(timeRows) ? timeRows : [];
                if (!rows.length) {
                    const msg = document.createElement("div");
                    msg.className = "text-xs text-gray-400 dark:text-gray-300";
                    msg.textContent = "No data for this range.";
                    timeFrag.appendChild(msg);
                } else {
                    rows.forEach((row) => {
                        const category = String(row?.category || "uncategorized");
                        const seconds = Number(row?.total_seconds || 0);
                        const color = this.getCategoryColor(category);
                        const line = document.createElement("div");
                        line.className = "flex items-center justify-between";
                        line.innerHTML = `
                            <div class="flex items-center gap-2">
                                <span class="w-2.5 h-2.5 rounded-full" style="background:${color}"></span>
                                <span class="font-medium" style="color:${color}">${this.escapeHtml(category)}</span>
                            </div>
                            <span class="font-medium text-gray-900 dark:text-white">${this.fmtDuration(seconds)}</span>
                        `;
                        timeFrag.appendChild(line);
                    });
                }
            } else {
                const msg = document.createElement("div");
                msg.className = "text-xs text-gray-400 dark:text-gray-300";
                msg.textContent = "Failed to load.";
                timeFrag.appendChild(msg);
            }

            const focusFrag = document.createDocumentFragment();
            if (focusRes.ok) {
                const focusRows = await focusRes.json();
                const rows = Array.isArray(focusRows) ? focusRows : [];
                if (!rows.length) {
                    const msg = document.createElement("div");
                    msg.className = "text-xs text-gray-400 dark:text-gray-300";
                    msg.textContent = "No data for this range.";
                    focusFrag.appendChild(msg);
                } else {
                    rows.forEach((row) => {
                        const category = String(row?.category || "uncategorized");
                        const focusedPct = Number(row?.focused_pct ?? 0);
                        const unfocusedPct = Number(row?.unfocused_pct ?? 0);
                        const color = this.getCategoryColor(category);
                        const line = document.createElement("div");
                        line.className = "flex items-center justify-between";
                        line.innerHTML = `
                            <div class="flex items-center gap-2">
                                <span class="w-2.5 h-2.5 rounded-full" style="background:${color}"></span>
                                <span class="font-medium" style="color:${color}">${this.escapeHtml(category)}</span>
                            </div>
                            <div class="flex items-center gap-2 text-xs">
                                <span class="text-emerald-600 dark:text-emerald-400">
                                      ${String(focusedPct.toFixed(0)).padStart(3, "0")}%
                                </span>
                                <span class="text-rose-600 dark:text-rose-400">
                                      ${String(unfocusedPct.toFixed(0)).padStart(3, "0")}%
                                </span>
                            </div>
                        `;
                        focusFrag.appendChild(line);
                    });
                }
            } else {
                const msg = document.createElement("div");
                msg.className = "text-xs text-gray-400 dark:text-gray-300";
                msg.textContent = "Failed to load.";
                focusFrag.appendChild(msg);
            }

            if (this._historyCategoryStatsToken !== token) return;
            await Promise.all([swapWithFade(timeEl, timeFrag), swapWithFade(focusEl, focusFrag)]);
        } catch (err) {
            if (this._historyCategoryStatsToken !== token) return;
            const timeFrag = document.createDocumentFragment();
            const focusFrag = document.createDocumentFragment();
            const msg1 = document.createElement("div");
            msg1.className = "text-xs text-gray-400 dark:text-gray-300";
            msg1.textContent = "Failed to load.";
            const msg2 = msg1.cloneNode(true);
            timeFrag.appendChild(msg1);
            focusFrag.appendChild(msg2);
            await Promise.all([swapWithFade(timeEl, timeFrag), swapWithFade(focusEl, focusFrag)]);
            console.warn("Failed to render history category stats", err);
        }
    }

    async renderHistory() {
        const days = this.historyDays;
        const list = document.getElementById("history-list");
        if (!list) return;

        const minAppSeconds = 60;

        // Prevent blink: keep existing DOM until the new content is ready,
        // then swap while faded out.
        const token = (this._historyListToken ?? 0) + 1;
        this._historyListToken = token;

        const nextFrame = () => new Promise((r) => requestAnimationFrame(() => r()));

        const startLoading = (el) => {
            el.style.willChange = "opacity";
            el.style.transition = "opacity 140ms ease";
            el.style.opacity = "0.55";
            if (!el.childElementCount) {
                el.innerHTML = `<div class="text-sm text-gray-400 dark:text-gray-500">Loading...</div>`;
            }
        };

        const swapWithFade = async (el, fragment) => {
            const prevHeight = el.getBoundingClientRect().height;
            if (prevHeight > 0) el.style.minHeight = `${prevHeight}px`;

            el.style.willChange = "opacity";
            el.style.transition = "opacity 350ms ease";
            el.style.opacity = "0";
            await nextFrame();
            if (this._historyListToken !== token) return;
            el.innerHTML = "";
            el.appendChild(fragment);
            await nextFrame();
            if (this._historyListToken !== token) return;
            el.style.opacity = "1";

            // Drop the min-height after the fade completes.
            window.setTimeout(() => {
                if (this._historyListToken === token) el.style.minHeight = "";
            }, 400);
        };

        startLoading(list);

        const res = await fetch(`/api/v1/focus/app-usage?days=${days}`, {
            cache: "no-store",
        });

        if (!res.ok) {
            const frag = document.createDocumentFragment();
            const msg = document.createElement("div");
            msg.className = "text-sm text-gray-400 dark:text-gray-500";
            msg.textContent = "Failed to load history.";
            frag.appendChild(msg);
            await swapWithFade(list, frag);
            return;
        }

        const data = await res.json();
        await this.renderHistoryCategoryStats(days);
        const dayKeys = Object.keys(data).sort((a, b) => (a < b ? 1 : -1));

        const listFrag = document.createDocumentFragment();

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

            const appEntriesAll = Object.entries(aggregated);
            const total = appEntriesAll.reduce((s, [, v]) => s + v, 0) || 0;

            let underMinuteSeconds = 0;
            const appEntries = appEntriesAll
                .filter(([, secs]) => {
                    if (Number(secs || 0) < minAppSeconds) {
                        underMinuteSeconds += Number(secs || 0);
                        return false;
                    }
                    return true;
                })
                .sort((a, b) => b[1] - a[1]);

            const colors = ["#2563eb", "#a855f7", "#f97316", "#ef4444", "#10b981", "#06b6d4"];
            const slices = [];
            let others = underMinuteSeconds;
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
                const goalSeconds = days * (3600 * GOAL_HOURS_PER_DAY);
                const pct = goalSeconds > 0 ? Math.min(100, (goalTotal / goalSeconds) * 100) : 0;
                goalBar.style.width = `${pct}%`;
            }
        } catch (err) {
            console.warn("Failed to render history pie/legend", err);
        }

        if (!dayKeys.length) {
            const msg = document.createElement("div");
            msg.className = "text-sm text-gray-400 dark:text-gray-500";
            msg.textContent = "No history available yet.";
            listFrag.appendChild(msg);

            if (this._historyListToken !== token) return;
            await swapWithFade(list, listFrag);
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

            const card = document.createElement("div");
            card.className = "bg-white dark:bg-gray-800 shadow-sm rounded-xl overflow-hidden shadow-subtle";
            card.style.opacity = String(opacity);

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

            const body = document.createElement("div");
            body.className = "p-5";

            const grid = document.createElement("div");
            grid.className = "grid grid-cols-1 sm:grid-cols-2 gap-3";

            let dayOthersSeconds = 0;

            const appRows = Object.entries(apps || {})
                .map(([appId, titles]) => {
                    let appTotal = 0;
                    Object.values(titles || {}).forEach((sec) => (appTotal += Number(sec || 0)));
                    return { appId, titles, appTotal };
                })
                .sort((a, b) => {
                    const aIsOthers = String(a.appId) === "Others";
                    const bIsOthers = String(b.appId) === "Others";
                    if (aIsOthers !== bIsOthers) return aIsOthers ? 1 : -1;
                    return b.appTotal - a.appTotal;
                });

            appRows.forEach(({ appId, titles, appTotal }) => {
                const badgeStyle = this.appIdBadgeStyle(appId);

                if (appTotal < minAppSeconds) {
                    dayOthersSeconds += appTotal;
                    return;
                }

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

                entries.forEach(([_, seconds]) => {
                    if (seconds < 60) {
                        othersSeconds += seconds;
                    }
                });

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

            if (dayOthersSeconds > 0) {
                const badgeStyle = this.appIdBadgeStyle("Others");

                const appBlock = document.createElement("div");
                appBlock.className =
                    "rounded-lg border border-gray-200 dark:border-gray-600 p-3 bg-gray-50 dark:bg-gray-800";

                appBlock.innerHTML = `
                <div class="flex items-center justify-between mb-2">
                    <span class="px-2 py-0.5 rounded text-[11px] font-bold border"
                        style="background:${badgeStyle.background};
                               color:${badgeStyle.color};
                               border-color:${badgeStyle.borderColor}">
                        Others
                    </span>
                    <span class="text-xs font-bold text-primary">
                        ${this.fmtDuration(dayOthersSeconds)}
                    </span>
                </div>
            `;

                const titleList = document.createElement("div");
                titleList.className = "flex flex-col gap-1 pl-2";

                const row = document.createElement("div");
                row.className = "flex justify-between text-sm text-gray-600 dark:text-gray-400 italic";
                row.innerHTML = `
                        <span>Apps &lt; 1m</span>
                        <span class="text-xs font-mono">${this.fmtDuration(dayOthersSeconds)}</span>
                    `;
                titleList.appendChild(row);

                appBlock.appendChild(titleList);
                grid.appendChild(appBlock);
            }

            body.appendChild(grid);
            card.appendChild(header);
            card.appendChild(body);
            listFrag.appendChild(card);
        });

        if (this._historyListToken !== token) return;
        await swapWithFade(list, listFrag);
    }

    async updateDailyFocus() {
        const days = 1;
        const res = await API.loadFocusToday(days);
        const container = document.getElementById("i-was-focused");
        const bar = document.getElementById("focus-progress-bar");
        const text = container?.querySelector("p");
        const pie = document.getElementById("stats-pie");
        const legend = document.getElementById("stats-legend");
        const totalEl = document.getElementById("stats-total");
        const totalText = document.getElementById("stats-text");
        const loadingEl = document.getElementById("stats-pie-loading");

        if (!container || !bar || !text || !pie || !totalEl || !totalText) return;

        const isDark = document.documentElement.classList.contains("dark");
        const applyEmptyStateTheme = () => {
            // Tailwind classes used here are static; use inline styles for theme-aware empty/error states.
            container.style.background = isDark
                ? "linear-gradient(135deg, #374151, #1f2937)"
                : "linear-gradient(135deg, #e5e7eb, #d1d5db)";
            container.style.color = isDark ? "#f9fafb" : "#111827";
            bar.style.backgroundColor = isDark ? "#6b7280" : "#d1d5db";
            pie.style.background = isDark ? "#1f2937" : "#e5e7eb";
        };
        const clearEmptyStateTheme = () => {
            container.style.background = "";
            container.style.color = "";
            bar.style.backgroundColor = "";
        };

        totalText.textContent = "Total Time";
        if (loadingEl) loadingEl.style.display = "block";

        if (!res.ok) {
            console.error("API '/focus/today' not returned ok", res.status, res.errorText || "");

            applyEmptyStateTheme();

            bar.style.width = "0%";
            bar.className = bar.className.replace(/bg-\S+/g, "").trim() + " bg-gray-300";
            container.className =
                container.className.replace(/from-\S+ to-\S+/, "").trim() +
                " bg-gradient-to-br from-gray-200 to-gray-300";
            totalEl.textContent = this.fmtDuration(0);

            const rawMsg = res.errorText || "Failed to load focus data from server.";
            const msg =
                typeof this.truncateText === "function" ? this.truncateText(String(rawMsg), 140) : String(rawMsg);
            text.textContent = `Failed to load focus data: ${msg}`;

            if (legend) {
                legend.innerHTML = `
                    <div class="text-xs text-gray-500 dark:text-gray-400">No focus summary available.</div>
                `;
            }

            if (loadingEl) loadingEl.style.display = "none";
            return;
        }

        clearEmptyStateTheme();

        const data = res.data;

        const focusedSeconds = Number(data.focused_seconds ?? 0);
        const unfocusedSeconds = Number(data.unfocused_seconds ?? 0);
        const totalSeconds = focusedSeconds + unfocusedSeconds;

        const aggregated = {};
        Object.keys(data || {}).forEach((d) => {
            const apps = data[d] || {};
            Object.entries(apps).forEach(([appId, titles]) => {
                let appTotal = 0;
                Object.values(titles || {}).forEach((sec) => (appTotal += Number(sec || 0)));
                aggregated[appId] = (aggregated[appId] || 0) + appTotal;
            });
        });

        try {
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
                else if (days <= 31)
                    monthLabel.textContent = now.toLocaleDateString(undefined, { month: "short", year: "numeric" });
                else monthLabel.textContent = now.toLocaleDateString(undefined, { month: "short", year: "numeric" });
            }

            if (goalText)
                goalText.textContent = `${this.fmtDuration(focusedSeconds)} over ${Object.keys(data).length} days`;
            if (goalBar) {
                const days = Number(this.historyDays) || 30;
                const goalSeconds = days * 3600;
                const pct = goalSeconds > 0 ? Math.min(100, (focusedSeconds / goalSeconds) * 100) : 0;
                goalBar.style.width = `${pct}%`;
            }
        } catch (err) {
            console.warn("Failed to render history pie/legend", err);
        }

        // (elements already resolved above)

        if (totalSeconds === 0) {
            applyEmptyStateTheme();

            bar.style.width = "0%";
            bar.className = bar.className.replace(/bg-\S+/, "").trim() + " bg-gray-300";

            container.className =
                container.className.replace(/from-\S+ to-\S+/, "").trim() +
                " bg-gradient-to-br from-gray-200 to-gray-300";

            text.textContent = "No focus data for today";

            totalEl.textContent = this.fmtDuration(0);

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
    }

    async updateMonitoringSummary() {
        const res = await API.loadMonitoringSummary();
        const container = document.getElementById("i-was-monitoring");
        const bar = document.getElementById("monitoring-progress-bar");
        const content = container?.querySelector(".relative.z-10");

        if (!container || !bar || !content) return;

        // Remove existing summary paragraphs (but keep the title)
        content.querySelectorAll("p").forEach((p) => p.remove());

        if (!res.ok) {
            bar.style.width = "0%";
            bar.className = bar.className.replace(/bg-\S+/g, "").trim() + " bg-gray-300";

            const rawMsg = res.errorText || "Failed to load monitoring summary.";
            const msg =
                typeof this.truncateText === "function" ? this.truncateText(String(rawMsg), 120) : String(rawMsg);

            const p = document.createElement("p");
            p.className = "text-xs font-medium text-white/90 dark:text-white/90 leading-tight";
            p.textContent = `Failed: ${msg}`;
            content.insertBefore(p, content.querySelector(".w-full"));

            return;
        }

        const data = res.data || {};
        const enabledSeconds = Number(data.monitoring_enabled_seconds ?? 0);
        const disabledSeconds = Number(data.monitoring_disabled_seconds ?? 0);
        const totalSeconds = enabledSeconds + disabledSeconds;

        const monitoringDominant = enabledSeconds >= disabledSeconds;

        const desiredGradient = monitoringDominant
            ? "bg-gradient-to-br from-emerald-500 to-teal-600"
            : "bg-gradient-to-br from-red-500 to-rose-600";

        container.className = container.className
            .replace(/bg-gradient-to-br\s+from-\S+\s+to-\S+/g, desiredGradient)
            .replace(/shadow-(emerald|red)-200\/40/g, "")
            .replace(/dark:shadow-(emerald|red)-900\/40/g, "")
            .replace(/\s+/g, " ")
            .trim();

        if (monitoringDominant) {
            container.className += " shadow-emerald-200/40 dark:shadow-emerald-900/40";
        } else {
            container.className += " shadow-red-200/40 dark:shadow-red-900/40";
        }

        const pct = totalSeconds > 0 ? (enabledSeconds / totalSeconds) * 100 : 0;
        bar.style.width = `${pct.toFixed(1)}%`;

        const enabledLabel = this.fmtDuration(enabledSeconds);
        const disabledLabel = this.fmtDuration(disabledSeconds);

        const summaryLineClass = "text-xs font-medium text-white/90 dark:text-white/90 leading-tight";

        const pEnabled = document.createElement("p");
        pEnabled.className = `${summaryLineClass} mb-1`;
        pEnabled.textContent = `Monitoring: ${enabledLabel}`;

        const pDisabled = document.createElement("p");
        pDisabled.className = `${summaryLineClass} mb-4`;
        pDisabled.textContent = `Not monitoring: ${disabledLabel}`;

        const progressWrapper = content.querySelector(".w-full");
        content.insertBefore(pEnabled, progressWrapper);
        content.insertBefore(pDisabled, progressWrapper);

        if (!/\bbg-/.test(bar.className)) {
            bar.className = bar.className.trim() + " bg-white";
        }
    }

    async updateHydrationSummary() {
        const res = await API.loadHydrationSummary();
        const container = document.getElementById("hydration");
        const bar = document.getElementById("hydration-bar");
        const content = container?.querySelector(".relative.z-10");

        if (!container || !bar || !content) return;

        content.querySelectorAll("p").forEach((p) => p.remove());

        const gradientByPct = (pct) => {
            if (pct < 35) return "bg-gradient-to-br from-amber-500 to-orange-600";
            if (pct < 65) return "bg-gradient-to-br from-sky-500 to-cyan-600";
            return "bg-gradient-to-br from-sky-500 to-blue-600";
        };

        if (!res.ok) {
            const p = document.createElement("p");
            p.className = "text-xs mb-4 font-medium text-white/90 dark:text-white/90 leading-tight";
            p.textContent = "Failed to load hydration data.";
            content.insertBefore(p, content.querySelector(".w-full"));
            bar.style.width = "0%";
            bar.className = bar.className.replace(/bg-\S+/g, "").trim() + " bg-white/60";
            container.className = container.className
                .replace(/bg-gradient-to-br\s+from-\S+\s+to-\S+/g, "")
                .replace(/\s+/g, " ")
                .trim();
            container.className += " bg-gradient-to-br from-amber-500 to-orange-600";
            return;
        }

        const data = res.data || {};
        const yes = Number(data.yes ?? 0);
        const no = Number(data.no ?? 0);
        const unknown = Number(data.unknown ?? 0);
        const total = Number(data.total ?? yes + no + unknown);

        const knownTotal = yes + no;
        const inferredDrinkChance = knownTotal > 0 ? yes / knownTotal : 0.5;
        const estimatedYes = yes + unknown * inferredDrinkChance;
        const estimatedNo = no + unknown * (1 - inferredDrinkChance);
        const pct = total > 0 ? Math.max(0, Math.min(100, (estimatedYes / total) * 100)) : 0;

        container.className = container.className
            .replace(/bg-gradient-to-br\s+from-\S+\s+to-\S+/g, "")
            .replace(/\s+/g, " ")
            .trim();
        container.className += ` ${gradientByPct(pct)}`;

        bar.style.width = `${pct.toFixed(1)}%`;
        if (!/\bbg-/.test(bar.className)) {
            bar.className = bar.className.trim() + " bg-white";
        }

        const pTop = document.createElement("p");
        pTop.className = "text-xs font-medium text-white/90 dark:text-white/90 leading-tight mb-1";
        pTop.textContent = total > 0 ? `Estimated: ${pct.toFixed(0)}% (last 24h)` : "No hydration responses yet";

        const pBottom = document.createElement("p");
        pBottom.className = "text-xs mb-4 font-medium text-white/90 dark:text-white/90 leading-tight";
        pBottom.textContent =
            total > 0
                ? `Drinking: ${((estimatedYes / total) * 100).toFixed(0)}% • Not drinking: ${((estimatedNo / total) * 100).toFixed(0)}%`
                : "Drinking: 0% • Not drinking: 0%";

        const progressWrapper = content.querySelector(".w-full");
        content.insertBefore(pTop, progressWrapper);
        content.insertBefore(pBottom, progressWrapper);
    }

    async updateDailyActivities() {
        const tasksRes = await API.loadRecurringTasks();
        if (!tasksRes.ok) {
            console.error(`Failed to fetch recurring tasks (status: ${tasksRes.status}): ${tasksRes.errorText || ""}`);
            return;
        }

        const tasks = tasksRes.tasks;

        let durationsByName = new Map();
        try {
            const summaryRes = await API.loadDailyActivitiesTodaySummary();
            if (summaryRes.ok) {
                const summary = summaryRes.summary;
                if (Array.isArray(summary)) {
                    summary.forEach((row) => {
                        const name = String(row?.name || "");
                        const seconds = Number(row?.total_seconds || 0);
                        if (name) durationsByName.set(name, seconds);
                    });
                }
            } else {
                console.error(
                    `Failed to fetch daily activities summary (status: ${summaryRes.status}): ${summaryRes.errorText || ""}`,
                );
            }
        } catch (err) {
            console.error("Failed to fetch daily activities summary:", err);
        }

        const container = document.getElementById("daily-classes");
        if (!container) return;
        container.innerHTML = "";

        if (!tasks.length) {
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

            const colorClass = this.getDailyColorClass(task.color);

            taskDiv.innerHTML = `
            <div class="flex items-center gap-3 min-w-0">
                <div class="h-8 w-8 rounded ${colorClass} flex items-center justify-center">
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
                    ${this.fmtDuration(durationsByName.get(task.name) || 0)}
                </span>
                <button class="edit-btn hover:text-primary rounded hover:cursor-pointer" title="Edit activity">
                    <span class="material-symbols-outlined text-[18px]">edit</span>
                </button>
                <button class="exclude-btn hover:text-red-600 rounded hover:cursor-pointer">
                    <span class="material-symbols-outlined text-[18px]">delete</span>
                </button>
            </div>
        `;

            const editBtn = taskDiv.querySelector(".edit-btn");
            editBtn.addEventListener("click", () => {
                const modal = document.getElementById("daily-config-modal");
                const nameEl = document.getElementById("daily-name");
                const appIdsEl = document.getElementById("daily-appids");
                const appTitlesEl = document.getElementById("daily-apptitles");
                const iconEl = document.getElementById("daily-icon");
                const colorEl = document.getElementById("daily-color");

                if (nameEl) nameEl.value = String(task.name || "");
                if (appIdsEl) appIdsEl.value = Array.isArray(task.app_ids) ? task.app_ids.join(";") : "";
                if (appTitlesEl) {
                    appTitlesEl.value = Array.isArray(task.app_titles) ? task.app_titles.join(";") : "";
                }
                if (iconEl) iconEl.value = String(task.icon || "");
                if (colorEl) colorEl.value = String(task.color || "");

                if (modal) modal.classList.remove("hidden");
            });

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

    async excludeDailyActivity(taskName) {
        try {
            await API.deleteRecurringTask(taskName);
        } catch (err) {
            console.error("Error excluding task:", err);
        }
    }
}
