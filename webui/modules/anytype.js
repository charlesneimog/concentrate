import * as API from "../api/client.js";

export class AnytypeManager {
    constructor(app) {
        this.app = app;
        const methods = [
            "loadTasks",
            "getAnytypeWarningElement",
            "updateAnytypeWarning",
            "openAnytypeConfigModal",
            "closeAnytypeConfigModal",
            "setAnytypeStatus",
            "renderAnytypeSpaces",
            "createAnytypeChallenge",
            "createAnytypeApiKey",
            "loadAnytypeSpaces",
            "saveAnytypeSpace",
            "saveAnytypeConfig",
            "refreshAnytypeCache",
        ];
        methods.forEach((name) => {
            app[name] = this[name].bind(app);
        });

        // Keep old constructor-time bindings for handlers that are attached
        // directly as event listeners.
        app.openAnytypeConfigModal = app.openAnytypeConfigModal.bind(app);
        app.closeAnytypeConfigModal = app.closeAnytypeConfigModal.bind(app);
        app.createAnytypeChallenge = app.createAnytypeChallenge.bind(app);
        app.createAnytypeApiKey = app.createAnytypeApiKey.bind(app);
        app.loadAnytypeSpaces = app.loadAnytypeSpaces.bind(app);
        app.saveAnytypeSpace = app.saveAnytypeSpace.bind(app);
        app.saveAnytypeConfig = app.saveAnytypeConfig.bind(app);
        app.refreshAnytypeCache = app.refreshAnytypeCache.bind(app);
    }

    async loadTasks() {
        this.anytypeError = null;
        try {
            const { ok, errorText, tasks } = await API.loadAnytypeTasks();
            if (!ok) {
                this.anytypeError = errorText || "Failed to load Anytype tasks.";
                return [];
            }
            return Array.isArray(tasks) ? tasks : [];
        } catch (err) {
            console.error("Failed to load Anytype tasks", err);
            this.anytypeError = "Failed to load Anytype tasks.";
            return [];
        }
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
            const { ok, errorText, data } = await API.createAnytypeChallenge();
            if (!ok) {
                this.setAnytypeStatus(errorText || "Failed to create challenge.");
                return;
            }
            this.anytypeChallengeId = data?.challenge_id || "";
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
            const { ok, errorText } = await API.createAnytypeApiKey(challengeId, code);
            if (!ok) {
                this.setAnytypeStatus(errorText || "Failed to create API key.");
                return;
            }
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
            const { ok, errorText, spaces } = await API.loadAnytypeSpaces();
            if (!ok) {
                this.setAnytypeStatus(errorText || "Failed to load spaces.");
                this.renderAnytypeSpaces([]);
                return;
            }
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
            const { ok, errorText } = await API.saveAnytypeSpace(spaceId);
            if (!ok) {
                this.setAnytypeStatus(errorText || "Failed to save space.");
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
            const { ok, errorText } = await API.saveAnytypeConfig(api_key, space_id);
            if (!ok) {
                if (status) status.textContent = errorText || "Failed to save settings.";
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
            const { ok, errorText } = await API.refreshAnytypeCache();
            if (!ok) {
                if (status) status.textContent = errorText || "Failed to update.";
                return;
            }
            if (status) status.textContent = "Updated.";
            await this.refreshAll();
        } catch (err) {
            console.error("Failed to refresh Anytype cache", err);
            if (status) status.textContent = "Failed to update.";
        }
    }
}
