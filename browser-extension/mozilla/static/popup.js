function isValidPort(value) {
  const port = Number(value);
  return Number.isInteger(port) && port >= 1 && port <= 65535;
}

function setStatus(text) {
  const el = document.getElementById("status");
  el.textContent = text;
}

async function getCurrentPort() {
  try {
    const resp = await browser.runtime.sendMessage({ type: "getPort" });
    if (resp && isValidPort(resp.port)) return resp.port;
  } catch (_) {
    // ignore; fall back to storage
  }

  try {
    const { port } = await browser.storage.local.get("port");
    if (isValidPort(port)) return Number(port);
  } catch (_) {
    // ignore
  }

  return 7079;
}

async function savePort(port) {
  const resp = await browser.runtime.sendMessage({ type: "setPort", port });
  if (!resp || resp.ok !== true) {
    throw new Error(resp?.error || "Failed to set port");
  }
}

document.addEventListener("DOMContentLoaded", async () => {
  const input = document.getElementById("port");
  const saveBtn = document.getElementById("save");

  const port = await getCurrentPort();
  input.value = String(port);

  saveBtn.addEventListener("click", async () => {
    const nextPort = Number(input.value);
    if (!isValidPort(nextPort)) {
      setStatus("Enter a valid port (1-65535).");
      return;
    }

    try {
      await savePort(nextPort);
      setStatus(`Saved. Using port ${nextPort}.`);
    } catch (e) {
      setStatus(e?.message || String(e));
    }
  });
});
