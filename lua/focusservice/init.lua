local M = {}

-- ─────────────────────────────────────
function M.send_project_status(port, payload)
	vim.fn.jobstart({
		"curl",
		"-s",
		"-X",
		"POST",
		"-H",
		"Content-Type: application/json",
		"-d",
		payload,
		"http://localhost:" .. port .. "/api/v1/special_project",
	}, {
		on_stdout = function(_, data)
			if data and #data > 0 then
				local filtered = {}
				for _, line in ipairs(data) do
					if line ~= "" then
						table.insert(filtered, line)
					end
				end
			end
		end,
		on_stderr = function(_, data)
			if data and #data > 0 then
				local filtered = {}
				for _, line in ipairs(data) do
					if line ~= "" then
						table.insert(filtered, line)
					end
				end
				if #filtered > 0 then
					vim.notify("Error: " .. table.concat(filtered, "\n"))
				end
			end
		end,
	})
end

-- ─────────────────────────────────────
function M.setup(config)
	local port
	if config.port ~= nil then
		port = config.port
	else
		port = 7079
	end

	-- Autocmds for focus/unfocus
	vim.api.nvim_create_autocmd("FocusGained", {
		callback = function()
			local project_name = vim.fn.fnamemodify(vim.fn.getcwd(), ":t")
			local payload = vim.fn.json_encode({ app_id = "Neovim", title = project_name, focus = true })
			M.send_project_status(port, payload)
		end,
	})

	vim.api.nvim_create_autocmd("FocusLost", {
		callback = function()
			local project_name = vim.fn.fnamemodify(vim.fn.getcwd(), ":t")
			local payload = vim.fn.json_encode({ app_id = "Neovim", title = project_name, focus = false })
			M.send_project_status(port, payload)
		end,
	})

	vim.api.nvim_create_autocmd("VimLeavePre", {
		callback = function()
			local project_name = vim.fn.fnamemodify(vim.fn.getcwd(), ":t")
			local payload = vim.fn.json_encode({
				title = project_name,
				app_id = "Neovim",
				focus = false,
			})
			M.send_project_status(port, payload)
		end,
	})
end

return M
