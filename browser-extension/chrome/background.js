let activeTabId = {
  app_id: null,
  title: null,
  focus: false,
};

const ACTION_ICON_DEFAULT = {
  32: "icons/32.png",
  48: "icons/48.png",
  128: "icons/128.png",
};

const ACTION_ICON_FOCUSED = {
  128: "icons/focused-128.png",
};

const ACTION_ICON_UNFOCUSED = {
  128: "icons/unfocused-128.png",
};

function setActionFocused(isFocused) {
  if (!chrome?.action) return;

  chrome.action.setTitle({
    title: isFocused ? "Concentrate (focused)" : "Concentrate (unfocused)",
  });

  chrome.action.setBadgeText({ text: isFocused ? "ON" : "" });
  chrome.action.setBadgeBackgroundColor({
    color: isFocused ? "#2E7D32" : "#666666",
  });

  const iconPath = isFocused ? ACTION_ICON_FOCUSED : ACTION_ICON_UNFOCUSED;
  chrome.action.setIcon({ path: iconPath || ACTION_ICON_DEFAULT });
}

let PORT = 7079;
let serverUrl = `http://localhost:${PORT}/api/v1/special_project`;

function setPort(newPort) {
  PORT = newPort;
  serverUrl = `http://localhost:${PORT}/api/v1/special_project`;
  console.log(`Port updated to ${PORT}`);
}

function isValidPort(value) {
  const port = Number(value);
  return Number.isInteger(port) && port >= 1 && port <= 65535;
}

async function loadPortFromStorage() {
  try {
    const { port } = await chrome.storage.local.get("port");
    if (isValidPort(port)) setPort(Number(port));
  } catch (e) {
    console.warn("Failed to load port from storage:", e);
  }
}

loadPortFromStorage();

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  (async () => {
    if (!message || typeof message !== "object") return;

    if (message.type === "getPort") {
      sendResponse({ port: PORT });
      return;
    }

    if (message.type === "setPort") {
      const nextPort = Number(message.port);
      if (!isValidPort(nextPort)) {
        sendResponse({ ok: false, error: "Invalid port" });
        return;
      }

      setPort(nextPort);
      try {
        await chrome.storage.local.set({ port: nextPort });
      } catch (_e) {
        sendResponse({ ok: false, error: "Failed to persist port" });
        return;
      }

      sendResponse({ ok: true, port: nextPort });
    }
  })();

  // Keep the message channel open for async sendResponse.
  return true;
});

function sendToServer(data) {
  if (!data || !data.app_id || !data.title) return;
  fetch(serverUrl, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify(data),
  })
    .then((response) => response.json())
    .then((data) => {
      console.log("Success:", data);
    })
    .catch((error) => {
      console.error("Error:", error);
    });
}

function cleanUrl(url) {
  try {
    let parsedUrl = new URL(url).origin;
    parsedUrl = parsedUrl.replace(/^https?:\/\//, "");
    parsedUrl = parsedUrl.replace(/^www\./, "");
    parsedUrl = parsedUrl.split("/")[0];
    return parsedUrl;
  } catch (e) {
    console.error("Invalid URL:", url);
    return null;
  }
}

chrome.tabs.onActivated.addListener(async ({ tabId }) => {
  const tab = await chrome.tabs.get(tabId);
  const appid = cleanUrl(tab.url);
  if (appid === "null") return;
  const title = tab.title;
  activeTabId = { app_id: appid, title: title, focus: true };
  setActionFocused(true);
  sendToServer(activeTabId);
});

chrome.tabs.onUpdated.addListener(async (_tabId, changeInfo, tab) => {
  if (!changeInfo.url) return;
  const appid = cleanUrl(changeInfo.url);
  if (appid === "null") return;

  const title = tab.title;
  setActionFocused(true);
  sendToServer({ app_id: appid, title: title, focus: true });
});

chrome.windows.onFocusChanged.addListener(async (windowId) => {
  if (windowId === chrome.windows.WINDOW_ID_NONE) {
    activeTabId.focus = false;
    setActionFocused(false);
    sendToServer(activeTabId);
  } else {
    const window = await chrome.windows.get(windowId, { populate: true });
    const activeTab = window.tabs?.find((tab) => tab.active);
    if (activeTab) {
      const appid = cleanUrl(activeTab.url);
      if (appid === "null") return;
      const title = activeTab.title;
      activeTabId = { app_id: appid, title: title, focus: true };
      setActionFocused(true);
      sendToServer(activeTabId);
    }
  }
});

setActionFocused(false);
