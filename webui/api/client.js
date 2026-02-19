async function readTextSafe(res) {
    try {
        return await res.text();
    } catch {
        return "";
    }
}

// ─────────────────────────────────────
export async function loadAnytypeTasks() {
    const res = await fetch("/api/v1/anytype/tasks", { cache: "no-store" });
    if (!res.ok) {
        const text = await readTextSafe(res);
        return { ok: false, errorText: text || "Failed to load Anytype tasks.", tasks: [] };
    }
    const tasks = await res.json();
    return { ok: true, errorText: "", tasks: Array.isArray(tasks) ? tasks : [] };
}

// ─────────────────────────────────────
export async function loadCurrent() {
    const res = await fetch("/api/v1/current", { cache: "no-store" });
    if (!res.ok) return null;
    const cur = await res.json();
    return Object.keys(cur || {}).length ? cur : null;
}

// ─────────────────────────────────────
export async function loadVersion() {
    const res = await fetch("/api/v1/version", { cache: "no-store" });
    if (!res.ok) return { ok: false, version: "" };

    try {
        const body = await res.json();
        const version = typeof body?.version === "string" ? body.version : "";
        return { ok: !!version, version };
    } catch {
        return { ok: false, version: "" };
    }
}

// ─────────────────────────────────────
export async function setMonitoringEnabled(enabled) {
    const res = await fetch("/api/v1/monitoring", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enabled: !!enabled }),
    });
    if (!res.ok) {
        const text = await readTextSafe(res);
        throw new Error(text || "Failed to toggle monitoring");
    }
    return true;
}

// ─────────────────────────────────────
export async function loadSettings() {
    const res = await fetch("/api/v1/settings", { cache: "no-store" });
    if (!res.ok) return null;
    return await res.json();
}

// ─────────────────────────────────────
export async function loadMonitoringSummary() {
    const res = await fetch("/api/v1/monitoring/summary", { cache: "no-store" });
    if (!res.ok) {
        const text = await readTextSafe(res);
        return {
            ok: false,
            status: res.status,
            errorText: text || "Failed to load monitoring summary.",
            data: null,
        };
    }

    try {
        return { ok: true, status: res.status, errorText: "", data: await res.json() };
    } catch {
        return {
            ok: false,
            status: res.status,
            errorText: "Invalid JSON from monitoring summary.",
            data: null,
        };
    }
}

// ─────────────────────────────────────
export async function loadHydrationSummary() {
    const res = await fetch("/api/v1/hydration/summary", { cache: "no-store" });
    if (!res.ok) {
        const text = await readTextSafe(res);
        return {
            ok: false,
            status: res.status,
            errorText: text || "Failed to load hydration summary.",
            data: null,
        };
    }

    try {
        return { ok: true, status: res.status, errorText: "", data: await res.json() };
    } catch {
        return {
            ok: false,
            status: res.status,
            errorText: "Invalid JSON from hydration summary.",
            data: null,
        };
    }
}

// ─────────────────────────────────────
export async function loadEvents() {
    const res = await fetch("/api/v1/events", { cache: "no-store" });
    if (!res.ok) return [];
    const events = await res.json();
    return Array.isArray(events) ? events : [];
}

// ─────────────────────────────────────
export async function loadHistoryRaw() {
    const res = await fetch("/api/v1/history", { cache: "no-store" });
    if (!res.ok) return [];
    const history = await res.json();
    return Array.isArray(history) ? history : [];
}

// ─────────────────────────────────────
export async function updateServerFocusRules(task) {
    const payload = task
        ? {
              allowed_app_ids: Array.isArray(task.allowed_app_ids) ? task.allowed_app_ids : [],
              allowed_titles: Array.isArray(task.allowed_titles) ? task.allowed_titles : [],
              task_title: task.title || "",
          }
        : { allowed_app_ids: [], allowed_titles: [], task_title: "" };

    const res = await fetch("/api/v1/focus/rules", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
    });

    if (!res.ok) {
        const text = await readTextSafe(res);
        throw new Error(text || "Failed to update focus rules");
    }

    return true;
}

// ─────────────────────────────────────
export async function setCurrentTaskOnServer(taskId) {
    const res = await fetch("/api/v1/task/set_current", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id: taskId }),
    });
    return res;
}

// ─────────────────────────────────────
export async function loadAnytypeTasksCategories() {
    const res = await fetch("/api/v1/anytype/tasks_categories", { cache: "no-store" });
    if (!res.ok) return { ok: false, categories: [] };
    const body = await res.json();
    const categories = Array.isArray(body?.data) ? body.data : [];
    return { ok: true, categories };
}

// ─────────────────────────────────────
export async function loadFocusCategoryPercentages(days) {
    const res = await fetch(`/api/v1/focus/category-percentages?days=${days}`, {
        method: "GET",
        headers: { "Content-Type": "application/json" },
        cache: "no-store",
    });
    if (!res.ok) return [];
    const rows = await res.json();
    return Array.isArray(rows) ? rows : [];
}

// ─────────────────────────────────────
export async function loadFocusToday(days) {
    const res = await fetch(`/api/v1/focus/today?days=${days}`, { cache: "no-store" });
    if (!res.ok) {
        const text = await readTextSafe(res);
        return { ok: false, status: res.status, errorText: text || "Failed to load focus today.", data: null };
    }

    try {
        return { ok: true, status: res.status, errorText: "", data: await res.json() };
    } catch {
        return { ok: false, status: res.status, errorText: "Invalid JSON from focus today.", data: null };
    }
}

// ─────────────────────────────────────
export async function loadFocusAppUsage(days) {
    const res = await fetch(`/api/v1/focus/app-usage?days=${days}`, { cache: "no-store" });
    if (!res.ok) return { ok: false, data: null };
    return { ok: true, data: await res.json() };
}

// ─────────────────────────────────────
export async function loadHistoryCategoryTime(days) {
    const res = await fetch(`/api/v1/history/category-time?days=${days}`, { cache: "no-store" });
    if (!res.ok) return { ok: false, rows: [] };
    const data = await res.json();
    return { ok: true, rows: Array.isArray(data) ? data : [] };
}

// ─────────────────────────────────────
export async function loadHistoryCategoryFocus(days) {
    const res = await fetch(`/api/v1/history/category-focus?days=${days}`, { cache: "no-store" });
    if (!res.ok) return { ok: false, rows: [] };
    const data = await res.json();
    return { ok: true, rows: Array.isArray(data) ? data : [] };
}

//╭─────────────────────────────────────╮
//│ Daily activities / recurring tasks  │
//╰─────────────────────────────────────╯
export async function loadRecurringTasks() {
    const res = await fetch("/api/v1/task/recurring_tasks", { cache: "no-store" });
    if (!res.ok) {
        const text = await readTextSafe(res);
        return { ok: false, status: res.status, errorText: text, tasks: [] };
    }
    const tasks = await res.json();
    return { ok: true, status: res.status, errorText: "", tasks: Array.isArray(tasks) ? tasks : [] };
}

// ─────────────────────────────────────
export async function loadDailyActivitiesSummaryToday() {
    const res = await fetch("/api/v1/daily_activities/today", { cache: "no-store" });
    if (!res.ok) {
        const text = await readTextSafe(res);
        return { ok: false, status: res.status, errorText: text, summary: [] };
    }
    const summary = await res.json();
    return { ok: true, status: res.status, errorText: "", summary: Array.isArray(summary) ? summary : [] };
}

//╭─────────────────────────────────────╮
//│   Backwards-compat alias used by    │
//│         modules/history.js          │
//╰─────────────────────────────────────╯
export async function loadDailyActivitiesTodaySummary() {
    return await loadDailyActivitiesSummaryToday();
}

// ─────────────────────────────────────
export async function excludeRecurringTask(taskName) {
    const url = `/api/v1/task/recurring_tasks?name=${encodeURIComponent(taskName)}`;
    const res = await fetch(url, { method: "DELETE" });
    return { ok: res.ok, errorText: res.ok ? "" : await readTextSafe(res) };
}

// ─────────────────────────────────────
export async function saveRecurringTask(data) {
    const res = await fetch("/api/v1/task/recurring_tasks", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(data),
    });
    return { ok: res.ok, errorText: res.ok ? "" : await readTextSafe(res) };
}

//╭─────────────────────────────────────╮
//│         Anytype auth/config         │
//╰─────────────────────────────────────╯
export async function createAnytypeChallenge() {
    const res = await fetch("/api/v1/anytype/auth/challenges", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
    });
    if (!res.ok) return { ok: false, errorText: await readTextSafe(res), data: null };
    return { ok: true, errorText: "", data: await res.json() };
}

// ─────────────────────────────────────
export async function createAnytypeApiKey(challengeId, code) {
    const res = await fetch("/api/v1/anytype/auth/api_keys", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ challenge_id: challengeId, code }),
    });
    if (!res.ok) return { ok: false, errorText: await readTextSafe(res), data: null };
    return { ok: true, errorText: "", data: await res.json() };
}

// ─────────────────────────────────────
export async function loadAnytypeSpaces() {
    const res = await fetch("/api/v1/anytype/spaces", { cache: "no-store" });
    if (!res.ok) return { ok: false, errorText: await readTextSafe(res), spaces: [] };
    const data = await res.json();
    const spaces = Array.isArray(data?.data) ? data.data : [];
    return { ok: true, errorText: "", spaces };
}

// ─────────────────────────────────────
export async function saveAnytypeSpace(spaceId) {
    const res = await fetch("/api/v1/anytype/space", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ space_id: spaceId }),
    });
    if (!res.ok) return { ok: false, errorText: await readTextSafe(res) };
    return { ok: true, errorText: "" };
}

// ─────────────────────────────────────
// BUG: Not implemented on the server
export async function saveAnytypeConfig(apiKey, spaceId) {
    const res = await fetch("/api/v1/anytype/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ api_key: apiKey, space_id: spaceId }),
    });
    if (!res.ok) return { ok: false, errorText: await readTextSafe(res) };
    return { ok: true, errorText: "" };
}

// ─────────────────────────────────────
// BUG: Not implemented on the server
export async function refreshAnytypeCache() {
    const res = await fetch("/api/v1/anytype/refresh", { method: "POST" });
    if (!res.ok) return { ok: false, errorText: await readTextSafe(res) };
    return { ok: true, errorText: "" };
}

//╭─────────────────────────────────────╮
//│              Pomodoro               │
//╰─────────────────────────────────────╯
export async function loadPomodoroState() {
    try {
        const res = await fetch("/api/v1/pomodoro/state", { cache: "no-store" });
        if (!res.ok) return null;
        return await res.json();
    } catch {
        return null;
    }
}

// ─────────────────────────────────────
export async function savePomodoroState(payload) {
    const res = await fetch("/api/v1/pomodoro/state", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
    });
    return res.ok;
}

// ─────────────────────────────────────
export async function loadPomodoroToday() {
    const res = await fetch("/api/v1/pomodoro/today", { cache: "no-store" });
    if (!res.ok) return null;
    return await res.json();
}

// ─────────────────────────────────────
export async function incrementPomodoroFocusComplete(focusSeconds) {
    const res = await fetch("/api/v1/pomodoro/focus/complete", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ focus_seconds: Math.max(0, Number(focusSeconds) || 0) }),
    });
    if (!res.ok) return null;
    return await res.json();
}
