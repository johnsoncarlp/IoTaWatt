// codearea: the file editor's text editor. A plain <textarea> (the browser's own editing, undo and find)
// over a copy of the text coloured by Prism, with line numbers. It offers the few calls the file editor
// used from Ace: setValue, getValue, clearSelection, setReadOnly, setMode, focus and on("change").
// Keys: Tab / Shift+Tab indent and outdent (2 spaces), Enter keeps the indent, Ctrl+S (Cmd+S) saves.
// Text longer than COLOUR_LIMIT is shown without colours (colouring it on every key would be slow).

(function () {
    var COLOUR_LIMIT = 50000;
    var INDENT = "  ";

    function escapeHTML(s) {
        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    window.createCodeArea = function (element) {
        var host = typeof element == "string" ? document.getElementById(element) : element;
        host.classList.add("codearea");
        host.innerHTML = '<div class="ca-gutter" aria-hidden="true"><pre></pre></div>' +
            '<div class="ca-body"><pre class="ca-colour" aria-hidden="true"><code></code></pre>' +
            '<textarea spellcheck="false" autocapitalize="off" autocomplete="off" wrap="off" aria-label="File contents"></textarea></div>';
        var gutter = host.querySelector(".ca-gutter pre");
        var colour = host.querySelector(".ca-colour");
        var code = colour.querySelector("code");
        var text = host.querySelector("textarea");
        var language = null;                                // a Prism language name, or null for plain text
        var listeners = [];
        var saveHandler = null;
        var lines = 0;
        var pending = false;

        function render() {
            pending = false;
            var value = text.value;
            var n = value.split("\n").length;
            if (n != lines) {
                var numbers = [];
                for (var i = 1; i <= n; i++) numbers.push(i);
                gutter.textContent = numbers.join("\n");
                lines = n;
            }
            var plain = !language || !window.Prism || !Prism.languages[language] || value.length > COLOUR_LIMIT;
            host.classList.toggle("ca-plain", plain);
            // A last empty line still needs its height in the coloured copy.
            code.innerHTML = plain ? "" :
                Prism.highlight(value, Prism.languages[language], language) + (value.slice(-1) == "\n" ? " " : "");
            syncScroll();
        }
        function schedule() {
            if (pending) return;
            pending = true;
            requestAnimationFrame(render);
        }
        function syncScroll() {
            colour.style.transform = "translate(" + -text.scrollLeft + "px," + -text.scrollTop + "px)";
            gutter.style.transform = "translateY(" + -text.scrollTop + "px)";
        }
        function changed() {
            schedule();
            listeners.forEach(function (fn) { fn(); });
        }

        // Insert or replace text as typing does, so the browser's undo (Ctrl+Z) keeps working.
        function replaceSelection(start, end, insert) {
            text.focus();
            text.setSelectionRange(start, end);
            if (!document.execCommand || !document.execCommand("insertText", false, insert)) {
                text.setRangeText(insert, start, end, "end");
                changed();
            }
        }

        text.addEventListener("input", changed);
        text.addEventListener("scroll", syncScroll);
        text.addEventListener("keydown", function (e) {
            if ((e.ctrlKey || e.metaKey) && !e.altKey && e.key.toLowerCase() == "s") {
                e.preventDefault();
                if (saveHandler) saveHandler();
                return;
            }
            if (text.readOnly) return;
            var v = text.value, start = text.selectionStart, end = text.selectionEnd;
            if (e.key == "Tab" && !e.ctrlKey && !e.altKey && !e.metaKey) {
                e.preventDefault();
                var lineStart = v.lastIndexOf("\n", start - 1) + 1;
                if (!e.shiftKey && start == end) {                      // indent at the caret
                    replaceSelection(start, end, INDENT);
                    return;
                }
                var blockEnd = end > start && v[end - 1] == "\n" ? end - 1 : end;
                var block = v.slice(lineStart, blockEnd);
                var out = e.shiftKey ?
                    block.replace(/^ {1,2}/gm, "") :
                    block.replace(/^/gm, INDENT);
                replaceSelection(lineStart, blockEnd, out);
                text.setSelectionRange(lineStart, lineStart + out.length);
                return;
            }
            if (e.key == "Enter" && !e.ctrlKey && !e.altKey && !e.metaKey && !e.shiftKey) {
                var ls = v.lastIndexOf("\n", start - 1) + 1;
                var indent = /^[ \t]*/.exec(v.slice(ls, start))[0];
                var before = v.slice(ls, start).trim().slice(-1);
                if ("{[(".indexOf(before) >= 0 && before) indent += INDENT;   // one more after an opening bracket
                e.preventDefault();
                replaceSelection(start, end, "\n" + indent);
            }
        });

        var editor = {
            setValue: function (value) {
                text.value = value;                       // a new file: its own undo history
                text.scrollTop = 0;
                text.scrollLeft = 0;
                lines = 0;
                render();
            },
            getValue: function () { return text.value; },
            clearSelection: function () { text.setSelectionRange(0, 0); },
            setReadOnly: function (on) { text.readOnly = !!on; host.classList.toggle("ca-readonly", !!on); },
            setMode: function (lang) { language = lang || null; render(); },
            focus: function () { text.focus(); },
            on: function (event, fn) { if (event == "change") listeners.push(fn); },
            onSave: function (fn) { saveHandler = fn; },
            textarea: text,
        };
        render();
        return editor;
    };
})();
