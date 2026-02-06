import * as API from "../api/client.js";

export class TaskView {
    constructor(app) {
        this.app = app;
        const methods = [
            "groupTasksByPriority",
            "renderTasks",
            "renderCurrentTask",
            "renderTaskMarkdown",
            "convertMarkdownToHtml",
            "ensureCategoryColors",
            "updateTasksCategories",
        ];
        methods.forEach((name) => {
            app[name] = this[name].bind(app);
        });
    }

    groupTasksByPriority(tasks) {
        const grouped = new Map();

        (Array.isArray(tasks) ? tasks : []).forEach((task) => {
            // priority may be null
            const raw = typeof task?.priority?.name === "string" ? task.priority.name.trim() : "";

            const key = raw === "" ? "No urgency" : /^P\d+$/.test(raw) ? raw : "No urgency";

            if (!grouped.has(key)) grouped.set(key, []);
            grouped.get(key).push(task);
        });

        return new Map(
            [...grouped.entries()].sort(([a], [b]) => {
                if (a === "No urgency") return 1;
                if (b === "No urgency") return -1;
                return parseInt(a.slice(1), 10) - parseInt(b.slice(1), 10);
            }),
        );
    }

    async ensureCategoryColors() {
        if (this.categoryColorMap && Object.keys(this.categoryColorMap).length) return;
        try {
            const res = await API.loadAnytypeTasksCategories();
            if (!res.ok) return;
            const categories = res.categories || [];
            categories.forEach((cat) => {
                if (!cat?.name || !cat?.color) return;
                this.categoryColorMap[String(cat.name).toLowerCase()] = `var(--anytype-color-tag-${cat.color})`;
            });
        } catch (err) {
            console.warn("Failed to load category colors", err);
        }
    }

    async updateTasksCategories() {
        const categoriesRes = await API.loadAnytypeTasksCategories();
        if (!categoriesRes.ok) return;

        const days = 1;
        const percentages = await API.loadFocusCategoryPercentages(days);

        const categories = categoriesRes.categories;
        this.categoryColorMap = {};
        categories.forEach((cat) => {
            if (!cat?.name || !cat?.color) return;
            this.categoryColorMap[String(cat.name).toLowerCase()] = `var(--anytype-color-tag-${cat.color})`;
        });
        const legend = document.getElementById("tasks-categories");
        if (!legend) return;

        legend.innerHTML = categories
            .map((cat) => {
                const percEntry = (percentages || []).find((p) => p.category === cat.name);
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
                console.log(task);
                const done = !!task.done;
                const isCurrent = this.currentTaskId && String(task.id) === String(this.currentTaskId);
                if (task.category === null) {
                    task.category = { select: { name: "Uncategorized", color: "red" } };
                }

                const categoryName =
                    typeof task.category.select.name === "string" ? task.category.select.name.trim() : "";
                const categoryColor = task.category.select.color;

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

                const categoryChip = document.createElement("span");
                categoryChip.className = "px-2 py-0.5 rounded-full text-[10px] font-medium border whitespace-nowrap";
                if (categoryName) {
                    categoryChip.textContent = categoryName;
                    if (categoryColor) {
                        categoryChip.style.setProperty("--c", `var(--anytype-color-tag-${categoryColor})`);
                        categoryChip.classList.add(
                            "bg-[color:var(--c)]/15",
                            "dark:bg-[color:var(--c)]/20",
                            "border-[color:var(--c)]",
                        );
                        categoryChip.style.color = `var(--anytype-color-tag-${categoryColor})`;
                    } else {
                        categoryChip.classList.add(
                            "bg-gray-50",
                            "dark:bg-gray-700",
                            "border-gray-200",
                            "dark:border-gray-600",
                            "text-gray-600",
                            "dark:text-gray-300",
                        );
                    }
                } else {
                    categoryChip.textContent = "Uncategorized";
                    categoryChip.classList.add(
                        "bg-gray-50",
                        "dark:bg-gray-700",
                        "border-gray-200",
                        "dark:border-gray-600",
                        "text-gray-500",
                        "dark:text-gray-400",
                    );
                }

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
                row.appendChild(categoryChip);
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

    async renderTaskMarkdown(task) {
        if (!task?.markdown) return;

        const markdown = task.markdown;
        const match = markdown.match(/## TO-DO[\s\S]*$/);
        if (!match) return null;

        const parsedMarkdown = match[0]
            .replace(/^\s*-\s*\[x\].*\n?/gim, "")
            .replace(/\n{3,}/g, "\n\n")
            .trim();

        const taskElement = document.querySelector(`[data-task-id="${task.id}"]`);
        if (!taskElement) {
            console.warn(`Task element not found for task ${task.id}`);
            return;
        }

        const html = this.convertMarkdownToHtml(parsedMarkdown);

        const existingContainer = taskElement.querySelector(".task-markdown-container");
        if (existingContainer) {
            existingContainer.remove();
        }

        const markdownContainer = document.createElement("div");
        markdownContainer.className = "task-markdown-container mt-3 pt-3 border-t border-gray-100 dark:border-gray-700";
        markdownContainer.innerHTML = html;
        taskElement.appendChild(markdownContainer);
    }

    convertMarkdownToHtml(markdown) {
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

        let html = processedMarkdown.replace(/`([^`]+)`/g, (_, code) => {
            const unescapedCode = code.replace(/\\_/g, "_").replace(/\\\*/g, "*").replace(/\\`/g, "`");
            const escapedCode = this.escapeHtml(unescapedCode);
            return `<code class="bg-gray-100 dark:bg-gray-700 px-1 py-0.5 rounded text-sm font-mono">${escapedCode}</code>`;
        });

        html = html.replace(/```([\s\S]*?)```/g, (_, code) => {
            const unescapedCode = code.replace(/\\_/g, "_").replace(/\\\*/g, "*").replace(/\\`/g, "`");
            const escapedCode = this.escapeHtml(unescapedCode.trim());
            return `<pre class="bg-gray-50 dark:bg-gray-800 p-3 rounded-lg overflow-x-auto my-2"><code class="text-sm font-mono">${escapedCode}</code></pre>`;
        });

        html = html.replace(/^### (.*$)/gm, (_, content) => {
            const processedContent = content.replace(
                /(?<!<\/?code[^>]*>)(?<!<\/?strong[^>]*>)(?<!<\/?em[^>]*>)[^<>]+/g,
                (text) => this.escapeHtml(text),
            );
            return `<h3 class="text-sm font-semibold text-gray-800 dark:text-gray-200 mt-3 mb-1">${processedContent}</h3>`;
        });

        html = html.replace(/^## (.*$)/gm, (_, content) => {
            const processedContent = content.replace(
                /(?<!<\/?code[^>]*>)(?<!<\/?strong[^>]*>)(?<!<\/?em[^>]*>)[^<>]+/g,
                (text) => this.escapeHtml(text),
            );
            return `<h2 class="text-base font-bold text-gray-900 dark:text-white mt-4 mb-2">${processedContent}</h2>`;
        });

        html = html.replace(/^# (.*$)/gm, (_, content) => {
            const processedContent = content.replace(
                /(?<!<\/?code[^>]*>)(?<!<\/?strong[^>]*>)(?<!<\/?em[^>]*>)[^<>]+/g,
                (text) => this.escapeHtml(text),
            );
            return `<h1 class="text-lg font-bold text-gray-900 dark:text-white mt-5 mb-3">${processedContent}</h1>`;
        });

        html = html.replace(/\*\*(.*?)\*\*|__(.*?)__/g, (match, p1, p2) => {
            const content = p1 || p2;
            if (match.includes("<code") || match.includes("</code>")) {
                return match;
            }
            const escapedContent = this.escapeHtml(content);
            return `<strong class="font-semibold">${escapedContent}</strong>`;
        });

        html = html.replace(/\*(.*?)\*|_(.*?)_/g, (match, p1, p2) => {
            const content = p1 || p2;
            if (match.includes("<code") || match.includes("</code>")) {
                return match;
            }
            const escapedContent = this.escapeHtml(content);
            return `<em class="italic">${escapedContent}</em>`;
        });

        html = html.replace(/^\s*[-*+] (\[[ x]\]\s*)?(.*$)/gm, (_, checkbox, content) => {
            let checkboxHtml = "";
            if (checkbox) {
                const isChecked = checkbox.includes("x");
                checkboxHtml = `<span class="pl-1 inline-flex items-center justify-center h-6 mr-3 font-mono text-sm ${
                    isChecked ? "text-emerald-600 font-semibold" : "text-gray-500"
                }">${isChecked ? "[✓]" : "[ ]"}</span>`;
            } else {
                checkboxHtml = '<span class="inline-flex items-center justify-center h-6 mr-3 text-gray-500">•</span>';
            }

            let contentHtml = content || "";
            if (contentHtml && !contentHtml.includes("<")) {
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

        const finalLines = html.split("\n");
        const processedLines = [];
        let currentParagraph = [];

        for (let i = 0; i < finalLines.length; i++) {
            const line = finalLines[i].trim();

            if (!line) {
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

        const finalHtml = `
        <div class="task-markdown-content prose prose-sm max-w-none">
            ${processedLines.join("\n")}
        </div>
    `;

        return finalHtml;
    }
}
