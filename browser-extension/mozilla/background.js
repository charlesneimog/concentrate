let activeTabId = {
  app_id: null,
  title: null,
  focus: false,
};

var PORT = 7079;
var serverUrl = `http://localhost:${PORT}/api/v1/special_project`;

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
    if (!browser?.storage?.local) return;
    const { port } = await browser.storage.local.get("port");
    if (isValidPort(port)) setPort(Number(port));
  } catch (e) {
    console.warn("Failed to load port from storage:", e);
  }
}

loadPortFromStorage();

browser.runtime.onMessage.addListener(async (message) => {
  if (!message || typeof message !== "object") return;

  if (message.type === "getPort") {
    return { port: PORT };
  }

  if (message.type === "setPort") {
    const nextPort = Number(message.port);
    if (!isValidPort(nextPort)) {
      return { ok: false, error: "Invalid port" };
    }

    setPort(nextPort);
    try {
      if (browser?.storage?.local) {
        await browser.storage.local.set({ port: nextPort });
      }
    } catch (e) {
      return { ok: false, error: "Failed to persist port" };
    }
    return { ok: true, port: nextPort };
  }
});

// Function to send data to the server
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

// Fix and Clean url
function cleanUrl(url) {
  try {
    var parsedUrl = new URL(url).origin;
    // remove https:// and http://
    parsedUrl = parsedUrl.replace(/^https?:\/\//, "");
    parsedUrl = parsedUrl.replace(/^www\./, ""); // remove www.
    parsedUrl = parsedUrl.split("/")[0]; // remove any path
    return parsedUrl;
  } catch (e) {
    console.error("Invalid URL:", url);
    return null;
  }
}

// Fires when user switches active tab
browser.tabs.onActivated.addListener(async ({ tabId, windowId }) => {
  const tab = await browser.tabs.get(tabId);
  // get clean URL without query parameters or fragments
  const appid = cleanUrl(tab.url);
  if (appid === "null") return; // ignore new tab page
  const title = tab.title;
  activeTabId = { app_id: appid, title: title, focus: true };
  // send to server
  sendToServer(activeTabId);
});

// Fires when user updates the URL of the active tab
browser.tabs.onUpdated.addListener(async (tabId, changeInfo, tab) => {
  if (!changeInfo.url) return; // ignore non-URL updates
  const appid = cleanUrl(changeInfo.url);
  if (appid === "null") return;

  // valid tab
  const title = tab.title;
  sendToServer({ app_id: appid, title: title, focus: true });
});

// Windows focus
browser.windows.onFocusChanged.addListener(async (windowId) => {
  if (windowId === browser.windows.WINDOW_ID_NONE) {
    activeTabId.focus = false;
    sendToServer(activeTabId);
  } else {
    const window = await browser.windows.get(windowId, { populate: true });
    const activeTab = window.tabs.find((tab) => tab.active);
    if (activeTab) {
      const appid = cleanUrl(activeTab.url);
      if (appid === "null") return; // ignore new tab page
      const title = activeTab.title;
      activeTabId = { app_id: appid, title: title, focus: true };
      sendToServer(activeTabId);
    }
  }  
});