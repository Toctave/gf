//////////////////////////////////////////////////////
// Source display:
//////////////////////////////////////////////////////

char autoPrintExpression[1024];
char autoPrintResult[1024];
int autoPrintExpressionLine;
int autoPrintResultLine;

bool DisplaySetPosition(const char *file, int line, bool useGDBToGetFullPath) {
	if (showingDisassembly) {
		return false;
	}

	char buffer[4096];
	const char *originalFile = file;

	if (file && file[0] == '~') {
		StringFormat(buffer, sizeof(buffer), "%s/%s", getenv("HOME"), 1 + file);
		file = buffer;
	} else if (file && file[0] != '/' && useGDBToGetFullPath) {
		EvaluateCommand("info source");
		const char *f = strstr(evaluateResult, "Located in ");

		if (f) {
			f += 11;
			const char *end = strchr(f, '\n');

			if (end) {
				StringFormat(buffer, sizeof(buffer), "%.*s", (int) (end - f), f);
				file = buffer;
			}
		}
	}

	bool reloadFile = false;

	if (file) {
		if (strcmp(currentFile, file)) {
			reloadFile = true;
		}

		struct stat buf;

		if (!stat(file, &buf) && buf.st_mtim.tv_sec != currentFileReadTime) {
			reloadFile = true;
		}

		currentFileReadTime = buf.st_mtim.tv_sec;
	}

	bool changed = false;

	if (reloadFile) {
		currentLine = 0;
		StringFormat(currentFile, 4096, "%s", file);
		realpath(currentFile, currentFileFull);

		size_t bytes;
		char *buffer2 = LoadFile(file, &bytes);

		if (!buffer2) {
			char buffer3[4096];
			StringFormat(buffer3, 4096, "The file '%s' (from '%s') could not be loaded.", file, originalFile);
			UICodeInsertContent(displayCode, buffer3, -1, true);
		} else {
			UICodeInsertContent(displayCode, buffer2, bytes, true);
			free(buffer2);
		}

		changed = true;
		autoPrintResult[0] = 0;
	}

	if (line != -1 && currentLine != line) {
		currentLine = line;
		UICodeFocusLine(displayCode, line);
		changed = true;
	}

	UIElementRefresh(&displayCode->e);

	return changed;
}

void DisplaySetPositionFromStack() {
	if (stack.Length() > stackSelected) {
		char location[sizeof(previousLocation)];
		strcpy(previousLocation, stack[stackSelected].location);
		strcpy(location, stack[stackSelected].location);
		char *line = strchr(location, ':');
		if (line) *line = 0;
		DisplaySetPosition(location, line ? atoi(line + 1) : -1, true);
	}
}

void DisassemblyLoad() {
	EvaluateCommand("disas");

	if (!strstr(evaluateResult, "Dump of assembler code for function")) {
		EvaluateCommand("disas $pc,+1000");
	}

	char *end = strstr(evaluateResult, "End of assembler dump.");

	if (!end) {
		printf("Disassembly failed. GDB output:\n%s\n", evaluateResult);
		return;
	}

	char *start = strstr(evaluateResult, ":\n");

	if (!start) {
		printf("Disassembly failed. GDB output:\n%s\n", evaluateResult);
		return;
	}

	start += 2;

	if (start >= end) {
		printf("Disassembly failed. GDB output:\n%s\n", evaluateResult);
		return;
	}

	char *pointer = strstr(start, "=> ");

	if (pointer) {
		pointer[0] = ' ';
		pointer[1] = ' ';
	}

	UICodeInsertContent(displayCode, start, end - start, true);
}

void DisassemblyUpdateLine() {
	EvaluateCommand("p $pc");
	char *address = strstr(evaluateResult, "0x");

	if (address) {
		char *addressEnd;
		uint64_t a = strtoul(address, &addressEnd, 0);

		for (int i = 0; i < 2; i++) {
			// Look for the line in the disassembly.

			bool found = false;

			for (int i = 0; i < displayCode->lineCount; i++) {
				uint64_t b = strtoul(displayCode->content + displayCode->lines[i].offset + 3, &addressEnd, 0);

				if (a == b) {
					UICodeFocusLine(displayCode, i + 1);
					autoPrintExpressionLine = i;
					found = true;
					break;
				}
			}

			if (!found) {
				// Reload the disassembly.
				DisassemblyLoad();
			} else {
				break;
			}
		}

		UIElementRefresh(&displayCode->e);
	}
}

void CommandToggleDisassembly(void *) {
	showingDisassembly = !showingDisassembly;
	autoPrintResultLine = 0;
	autoPrintExpression[0] = 0;
	displayCode->e.flags ^= UI_CODE_NO_MARGIN;

	if (showingDisassembly) {
		UICodeInsertContent(displayCode, "Disassembly could not be loaded.\nPress Ctrl+D to return to source view.", -1, true);
		displayCode->tabSize = 8;
		DisassemblyLoad();
		DisassemblyUpdateLine();
	} else {
		currentLine = -1;
		currentFile[0] = 0;
		currentFileReadTime = 0;
		DisplaySetPositionFromStack();
		displayCode->tabSize = 4;
	}

	UIElementRefresh(&displayCode->e);
}

int DisplayCodeMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED && !showingDisassembly) {
		int result = UICodeHitTest((UICode *) element, element->window->cursorX, element->window->cursorY);

		if (result < 0) {
			int line = -result;
			CommandToggleBreakpoint((void *) (intptr_t) line);
		} else if (result > 0) {
			int line = result;

			if (element->window->ctrl) {
				char buffer[1024];
				StringFormat(buffer, 1024, "until %d", line);
				DebuggerSend(buffer, true, false);
			} else if (element->window->alt) {
				char buffer[1024];
				StringFormat(buffer, 1024, "tbreak %d", line);
				EvaluateCommand(buffer);
				StringFormat(buffer, 1024, "jump %d", line);
				DebuggerSend(buffer, true, false);
			}
		}
	} else if (message == UI_MSG_CODE_GET_MARGIN_COLOR && !showingDisassembly) {
		for (int i = 0; i < breakpoints.Length(); i++) {
			if (breakpoints[i].line == di && 0 == strcmp(breakpoints[i].fileFull, currentFileFull)) {
				return 0xFF0000;
			}
		}
	} else if (message == UI_MSG_CODE_GET_LINE_HINT) {
		UITableGetItem *m = (UITableGetItem *) dp;

		if (m->index == autoPrintResultLine) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", autoPrintResult);
		}
	}

	return 0;
}

UIElement *SourceWindowCreate(UIElement *parent) {
	displayCode = UICodeCreate(parent, 0);
	displayCode->e.messageUser = DisplayCodeMessage;
	return &displayCode->e;
}

void SourceWindowUpdate(const char *data, UIElement *element) {
	bool changedSourceLine = false;

	const char *line = data;

	while (*line) {
		if (line[0] == '\n' || line == data) {
			int i = line == data ? 0 : 1, number = 0;

			while (line[i]) {
				if (line[i] == '\t') {
					break;
				} else if (isdigit(line[i])) {
					number = number * 10 + line[i] - '0';
					i++;
				} else {
					goto tryNext;
				}
			}

			if (!line[i]) break;
			if (number) changedSourceLine = true;
			tryNext:;
			line += i + 1;
		} else {
			line++;
		}
	}

	if (!stackChanged && changedSourceLine) stackSelected = 0;
	stackChanged = false;

	if (changedSourceLine && stackSelected < stack.Length() && strcmp(stack[stackSelected].location, previousLocation)) {
		DisplaySetPositionFromStack();
	}
	
	if (changedSourceLine && currentLine < displayCode->lineCount && currentLine > 0) {
		// If there is an auto-print expression from the previous line, evaluate it.

		if (autoPrintExpression[0]) {
			char buffer[1024];
			StringFormat(buffer, sizeof(buffer), "p %s", autoPrintExpression);
			EvaluateCommand(buffer);
			const char *result = strchr(evaluateResult, '=');

			if (result) {
				autoPrintResultLine = autoPrintExpressionLine;
				StringFormat(autoPrintResult, sizeof(autoPrintResult), "%s", result);
				char *end = strchr(autoPrintResult, '\n');
				if (end) *end = 0;
			} else {
				autoPrintResult[0] = 0;
			}

			autoPrintExpression[0] = 0;
		}

		// Parse the new source line.

		UICodeLine *line = displayCode->lines + currentLine - 1;
		const char *text = displayCode->content + line->offset;
		size_t bytes = line->bytes;
		uintptr_t position = 0;

		while (position < bytes) {
			if (text[position] != '\t') break;
			else position++;
		}

		uintptr_t expressionStart = position;

		{
			// Try to parse a type name.

			uintptr_t position2 = position;

			while (position2 < bytes) {
				char c = text[position2];
				if (!_UICharIsAlphaOrDigitOrUnderscore(c)) break;
				else position2++;
			}

			if (position2 == bytes) goto noTypeName;
			if (text[position2] != ' ') goto noTypeName;
			position2++;

			while (position2 < bytes) {
				if (text[position2] != '*') break;
				else position2++;
			}

			if (position2 == bytes) goto noTypeName;
			if (!_UICharIsAlphaOrDigitOrUnderscore(text[position2])) goto noTypeName;

			position = expressionStart = position2;
			noTypeName:;
		}

		while (position < bytes) {
			char c = text[position];
			if (!_UICharIsAlphaOrDigitOrUnderscore(c) && c != '[' && c != ']' && c != ' ' && c != '.' && c != '-' && c != '>') break;
			else position++;
		}

		uintptr_t expressionEnd = position;

		while (position < bytes) {
			if (text[position] != ' ') break;
			else position++;
		}

		if (position != bytes && text[position] == '=') {
			StringFormat(autoPrintExpression, sizeof(autoPrintExpression), "%.*s",
				(int) (expressionEnd - expressionStart), text + expressionStart);
		}

		autoPrintExpressionLine = currentLine;
	}

	UIElementRefresh(element);
}

//////////////////////////////////////////////////////
// Bitmap viewer:
//////////////////////////////////////////////////////

struct BitmapViewer {
	char pointer[256];
	char width[256];
	char height[256];
	char stride[256];
	int parsedWidth, parsedHeight;
	UIButton *autoToggle;
	UIImageDisplay *display;
	UIPanel *labelPanel;
	UILabel *label;
};

Array<UIElement *> autoUpdateBitmapViewers;
bool autoUpdateBitmapViewersQueued;

int BitmapViewerWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		free(element->cp);
	} else if (message == UI_MSG_GET_WIDTH) {
		return ((BitmapViewer *) element->cp)->parsedWidth;
	} else if (message == UI_MSG_GET_HEIGHT) {
		return ((BitmapViewer *) element->cp)->parsedHeight;
	}
	
	return 0;
}

void BitmapViewerUpdate(const char *pointerString, const char *widthString, const char *heightString, const char *strideString, UIElement *owner = nullptr);

int BitmapViewerRefreshMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		BitmapViewer *bitmap = (BitmapViewer *) element->parent->cp;
		BitmapViewerUpdate(bitmap->pointer, bitmap->width, bitmap->height, bitmap->stride, element->parent);
	}

	return 0;
}

int BitmapViewerAutoMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		element->flags ^= UI_BUTTON_CHECKED;

		if (element->flags & UI_BUTTON_CHECKED) {
			autoUpdateBitmapViewers.Add(element->parent);
		} else {
			bool found = false;

			for (int i = 0; i < autoUpdateBitmapViewers.Length(); i++) {
				if (autoUpdateBitmapViewers[i] == element->parent) {
					autoUpdateBitmapViewers.DeleteSwap(i);
					found = true;
					break;
				}
			}

			assert(found);
		}
	}

	return 0;
}

const char *BitmapViewerGetBits(const char *pointerString, const char *widthString, const char *heightString, const char *strideString,
		uint32_t **_bits, int *_width, int *_height, int *_stride) {
	const char *widthResult = EvaluateExpression(widthString);
	if (!widthResult) { return "Could not evaluate width."; }
	int width = atoi(widthResult + 1);
	const char *heightResult = EvaluateExpression(heightString);
	if (!heightResult) { return "Could not evaluate height."; }
	int height = atoi(heightResult + 1);
	int stride = width * 4;
	const char *pointerResult = EvaluateExpression(pointerString, "/x");
	if (!pointerResult) { return "Could not evaluate pointer."; }
	char _pointerResult[1024];
	StringFormat(_pointerResult, sizeof(_pointerResult), "%s", pointerResult);
	pointerResult = strstr(_pointerResult, " 0x");
	if (!pointerResult) { return "Pointer to image bits does not look like an address!"; }
	pointerResult++;

	if (strideString && *strideString) {
		const char *strideResult = EvaluateExpression(strideString);
		if (!strideResult) { return "Could not evaluate stride."; }
		stride = atoi(strideResult + 1);
	}

	uint32_t *bits = (uint32_t *) malloc(stride * height * 4);

	char buffer[1024];
	StringFormat(buffer, sizeof(buffer), "dump binary memory .bitmap.gf (%s) (%s+%d)", pointerResult, pointerResult, stride * height);
	EvaluateCommand(buffer);

	FILE *f = fopen(".bitmap.gf", "rb");

	if (f) {
		fread(bits, 1, stride * height * 4, f);
		fclose(f);
		unlink(".bitmap.gf");
	}

	if (!f || strstr(evaluateResult, "access")) {
		return "Could not read the image bits!";
	}

	*_bits = bits, *_width = width, *_height = height, *_stride = stride;
	return nullptr;
}

void BitmapViewerUpdate(const char *pointerString, const char *widthString, const char *heightString, const char *strideString, UIElement *owner) {
	uint32_t *bits = nullptr;
	int width = 0, height = 0, stride = 0;
	const char *error = BitmapViewerGetBits(pointerString, widthString, heightString, strideString,
			&bits, &width, &height, &stride);

	if (!owner) {
		BitmapViewer *bitmap = (BitmapViewer *) calloc(1, sizeof(BitmapViewer));
		if (pointerString) StringFormat(bitmap->pointer, sizeof(bitmap->pointer), "%s", pointerString);
		if (widthString) StringFormat(bitmap->width, sizeof(bitmap->width), "%s", widthString);
		if (heightString) StringFormat(bitmap->height, sizeof(bitmap->height), "%s", heightString);
		if (strideString) StringFormat(bitmap->stride, sizeof(bitmap->stride), "%s", strideString);

		UIMDIChild *window = UIMDIChildCreate(&dataWindow->e, UI_MDI_CHILD_CLOSE_BUTTON, UI_RECT_1(0), "Bitmap", -1);
		window->e.messageUser = BitmapViewerWindowMessage;
		window->e.cp = bitmap;
		bitmap->autoToggle = UIButtonCreate(&window->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "Auto", -1);
		bitmap->autoToggle->e.messageUser = BitmapViewerAutoMessage;
		UIButtonCreate(&window->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "Refresh", -1)->e.messageUser = BitmapViewerRefreshMessage;
		owner = &window->e;

		UIPanel *panel = UIPanelCreate(owner, UI_PANEL_EXPAND);
		bitmap->display = UIImageDisplayCreate(&panel->e, UI_IMAGE_DISPLAY_INTERACTIVE | UI_ELEMENT_V_FILL, bits, width, height, stride);
		bitmap->labelPanel = UIPanelCreate(&panel->e, UI_PANEL_GRAY | UI_ELEMENT_V_FILL);
		bitmap->label = UILabelCreate(&bitmap->labelPanel->e, UI_ELEMENT_H_FILL, nullptr, 0);
	}

	BitmapViewer *bitmap = (BitmapViewer *) owner->cp;
	bitmap->parsedWidth = width, bitmap->parsedHeight = height;
	UIImageDisplaySetContent(bitmap->display, bits, width, height, stride);
	if (error) UILabelSetContent(bitmap->label, error, -1);
	if (error) bitmap->labelPanel->e.flags &= ~UI_ELEMENT_HIDE, bitmap->display->e.flags |= UI_ELEMENT_HIDE;
	else bitmap->labelPanel->e.flags |= UI_ELEMENT_HIDE, bitmap->display->e.flags &= ~UI_ELEMENT_HIDE;
	UIElementRefresh(&bitmap->display->e);
	UIElementRefresh(&bitmap->label->e);
	UIElementRefresh(bitmap->labelPanel->e.parent);
	UIElementRefresh(owner);
	UIElementRefresh(&dataWindow->e);

	free(bits);
}

void BitmapAddDialog(void *) {
	static char *pointer = nullptr, *width = nullptr, *height = nullptr, *stride = nullptr;

	const char *result = UIDialogShow(windowMain, 0, 
			"Add bitmap\n\n%l\n\nPointer to bits: (32bpp, RR GG BB AA)\n%t\nWidth:\n%t\nHeight:\n%t\nStride: (optional)\n%t\n\n%l\n\n%f%b%b",
			&pointer, &width, &height, &stride, "Add", "Cancel");

	if (0 == strcmp(result, "Add")) {
		BitmapViewerUpdate(pointer, width, height, (stride && stride[0]) ? stride : nullptr);
	}
}

void BitmapViewerUpdateAll() {
	if (~dataTab->e.flags & UI_ELEMENT_HIDE) {
		for (int i = 0; i < autoUpdateBitmapViewers.Length(); i++) {
			BitmapViewer *bitmap = (BitmapViewer *) autoUpdateBitmapViewers[i]->cp;
			BitmapViewerUpdate(bitmap->pointer, bitmap->width, bitmap->height, bitmap->stride, autoUpdateBitmapViewers[i]);
		}
	} else if (autoUpdateBitmapViewers.Length()) {
		autoUpdateBitmapViewersQueued = true;
	}
}

//////////////////////////////////////////////////////
// Console:
//////////////////////////////////////////////////////

Array<char *> commandHistory;
int commandHistoryIndex;

int TextboxInputMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITextbox *textbox = (UITextbox *) element;

	if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		static TabCompleter tabCompleter = {};
		bool lastKeyWasTab = tabCompleter._lastKeyWasTab;
		tabCompleter._lastKeyWasTab = false;

		if (m->code == UI_KEYCODE_ENTER && !element->window->shift) {
			char buffer[1024];
			StringFormat(buffer, 1024, "%.*s", (int) textbox->bytes, textbox->string);
			if (commandLog) fprintf(commandLog, "%s\n", buffer);
			CommandSendToGDB(buffer);

			char *string = (char *) malloc(textbox->bytes + 1);
			memcpy(string, textbox->string, textbox->bytes);
			string[textbox->bytes] = 0;
			commandHistory.Insert(string, 0);
			commandHistoryIndex = 0;

			if (commandHistory.Length() > 100) {
				free(commandHistory.Last());
				commandHistory.Pop();
			}

			UITextboxClear(textbox, false);
			UIElementRefresh(&textbox->e);

			return 1;
		} else if (m->code == UI_KEYCODE_TAB && textbox->bytes && !element->window->shift) {
			TabCompleterRun(&tabCompleter, textbox, lastKeyWasTab, false);
			return 1;
		} else if (m->code == UI_KEYCODE_UP) {
			if (commandHistoryIndex < commandHistory.Length()) {
				UITextboxClear(textbox, false);
				UITextboxReplace(textbox, commandHistory[commandHistoryIndex], -1, false);
				if (commandHistoryIndex < commandHistory.Length() - 1) commandHistoryIndex++;
				UIElementRefresh(&textbox->e);
			}
		} else if (m->code == UI_KEYCODE_DOWN) {
			UITextboxClear(textbox, false);

			if (commandHistoryIndex > 0) {
				--commandHistoryIndex;
				UITextboxReplace(textbox, commandHistory[commandHistoryIndex], -1, false);
			}

			UIElementRefresh(&textbox->e);
		}
	}

	return 0;
}

UIElement *ConsoleWindowCreate(UIElement *parent) {
	UIPanel *panel2 = UIPanelCreate(parent, UI_PANEL_EXPAND);
	displayOutput = UICodeCreate(&panel2->e, UI_CODE_NO_MARGIN | UI_ELEMENT_V_FILL);
	UIPanel *panel3 = UIPanelCreate(&panel2->e, UI_PANEL_HORIZONTAL | UI_PANEL_EXPAND | UI_PANEL_GRAY);
	panel3->border = UI_RECT_1(5);
	panel3->gap = 5;
	trafficLight = UISpacerCreate(&panel3->e, 0, 30, 30);
	trafficLight->e.messageUser = TrafficLightMessage;
	UIButton *buttonMenu = UIButtonCreate(&panel3->e, 0, "Menu", -1);
	buttonMenu->invoke = InterfaceShowMenu;
	buttonMenu->e.cp = buttonMenu;
	UITextbox *textboxInput = UITextboxCreate(&panel3->e, UI_ELEMENT_H_FILL);
	textboxInput->e.messageUser = TextboxInputMessage;
	UIElementFocus(&textboxInput->e);
	return &panel2->e;
}

//////////////////////////////////////////////////////
// Watch window:
//////////////////////////////////////////////////////

struct Watch {
	bool open, hasFields, loadedFields, isArray;
	uint8_t depth;
	char format;
	uintptr_t arrayIndex;
	char *key, *value, *type;
	Array<Watch *> fields;
	Watch *parent;
	uint64_t updateIndex;
};

struct WatchWindow {
	Array<Watch *> rows;
	Array<Watch *> baseExpressions;
	UIElement *element;
	UITextbox *textbox;
	int selectedRow;
	uint64_t updateIndex;
	bool waitingForFormatCharacter;
};

struct WatchLogEntry {
	char value[24];
	char where[96];
};

struct WatchLogger {
	int id;
	Array<WatchLogEntry> entries;
	UITable *table;
};

Array<WatchLogger *> watchLoggers;

int WatchTextboxMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITextbox *textbox = (UITextbox *) element;

	if (message == UI_MSG_UPDATE) {
		if (element->window->focused != element) {
			UIElementDestroy(element);
			((WatchWindow *) element->cp)->textbox = nullptr;
		}
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		static TabCompleter tabCompleter = {};
		bool lastKeyWasTab = tabCompleter._lastKeyWasTab;
		tabCompleter._lastKeyWasTab = false;

		if (m->code == UI_KEYCODE_TAB && textbox->bytes && !element->window->shift) {
			TabCompleterRun(&tabCompleter, textbox, lastKeyWasTab, true);
			return 1;
		}
	}

	return 0;
}

void WatchDestroyTextbox(WatchWindow *w) {
	if (!w->textbox) return;
	UIElementDestroy(&w->textbox->e);
	w->textbox = nullptr;
	UIElementFocus(w->element);
}

void WatchFree(Watch *watch) {
	for (int i = 0; i < watch->fields.Length(); i++) {
		WatchFree(watch->fields[i]);
		if (!watch->isArray) free(watch->fields[i]);
	}

	if (watch->isArray) free(watch->fields[0]);
	free(watch->key);
	free(watch->value);
	free(watch->type);
	watch->fields.Free();
}

void WatchDeleteExpression(WatchWindow *w) {
	WatchDestroyTextbox(w);
	if (w->selectedRow == w->rows.Length()) return;
	int end = w->selectedRow + 1;

	for (; end < w->rows.Length(); end++) {
		if (w->rows[w->selectedRow]->depth >= w->rows[end]->depth) {
			break;
		}
	}

	bool found = false;
	Watch *watch = w->rows[w->selectedRow];

	for (int i = 0; i < w->baseExpressions.Length(); i++) {
		if (watch == w->baseExpressions[i]) {
			found = true;
			w->baseExpressions.Delete(i);
			break;
		}
	}

	assert(found);
	w->rows.Delete(w->selectedRow, end - w->selectedRow);
	WatchFree(watch);
	free(watch);
}

void WatchEvaluate(const char *function, Watch *watch) {
	char buffer[4096];
	uintptr_t position = 0;

	position += StringFormat(buffer + position, sizeof(buffer) - position, "py %s([", function);

	Watch *stack[32];
	int stackCount = 0;
	stack[0] = watch;

	while (stack[stackCount]) {
		stack[stackCount + 1] = stack[stackCount]->parent;
		stackCount++;
		if (stackCount == 32) break;
	}

	bool first = true;

	while (stackCount) {
		stackCount--;

		if (!first) {
			position += StringFormat(buffer + position, sizeof(buffer) - position, ",");
		} else {
			first = false;
		}

		if (stack[stackCount]->key) {
			position += StringFormat(buffer + position, sizeof(buffer) - position, "'%s'", stack[stackCount]->key);
		} else {
			position += StringFormat(buffer + position, sizeof(buffer) - position, "%lu", stack[stackCount]->arrayIndex);
		}
	}

	position += StringFormat(buffer + position, sizeof(buffer) - position, "]");

	if (0 == strcmp(function, "gf_valueof")) {
		position += StringFormat(buffer + position, sizeof(buffer) - position, ",'%c'", watch->format ?: ' ');
	}

	position += StringFormat(buffer + position, sizeof(buffer) - position, ")");

	EvaluateCommand(buffer);
}

bool WatchHasFields(Watch *watch) {
	WatchEvaluate("gf_fields", watch);

	if (strstr(evaluateResult, "(array)")) {
		return true;
	} else {
		char *position = evaluateResult;
		char *end = strchr(position, '\n');
		if (!end) return false;
		*end = 0;
		if (strstr(position, "(gdb)")) return false;
		return true;
	}
}

void WatchAddFields(Watch *watch) {
	if (watch->loadedFields) {
		return;
	}

	watch->loadedFields = true;

	WatchEvaluate("gf_fields", watch);

	if (strstr(evaluateResult, "(array)")) {
		int count = atoi(evaluateResult + 7) + 1;

		if (count > 10000000) {
			count = 10000000;
		}

		Watch *fields = (Watch *) calloc(count, sizeof(Watch));
		watch->isArray = true;
		bool hasSubFields = false;

		for (int i = 0; i < count; i++) {
			fields[i].parent = watch;
			fields[i].arrayIndex = i;
			watch->fields.Add(&fields[i]);
			if (!i) hasSubFields = WatchHasFields(&fields[i]);
			fields[i].hasFields = hasSubFields;
			fields[i].depth = watch->depth + 1;
		}
	} else {
		char *start = strdup(evaluateResult);
		char *position = start;

		while (true) {
			char *end = strchr(position, '\n');
			if (!end) break;
			*end = 0;
			if (strstr(position, "(gdb)")) break;
			Watch *field = (Watch *) calloc(1, sizeof(Watch));
			field->depth = watch->depth + 1;
			field->parent = watch;
			field->key = (char *) malloc(end - position + 1);
			strcpy(field->key, position);
			watch->fields.Add(field);
			field->hasFields = WatchHasFields(field);
			position = end + 1;
		}

		free(start);
	}
}

void WatchInsertFieldRows(WatchWindow *w, Watch *watch, int *position) {
	for (int i = 0; i < watch->fields.Length(); i++) {
		w->rows.Insert(watch->fields[i], *position);
		*position = *position + 1;

		if (watch->fields[i]->open) {
			WatchInsertFieldRows(w, watch->fields[i], position);
		}
	}
}

void WatchEnsureRowVisible(WatchWindow *w, int index) {
	if (w->selectedRow < 0) w->selectedRow = 0;
	else if (w->selectedRow > w->rows.Length()) w->selectedRow = w->rows.Length();
	UIScrollBar *scroll = ((UIPanel *) w->element->parent)->scrollBar;
	int rowHeight = (int) (UI_SIZE_TEXTBOX_HEIGHT * w->element->window->scale);
	int start = index * rowHeight, end = (index + 1) * rowHeight, height = UI_RECT_HEIGHT(w->element->parent->bounds);
	bool unchanged = false;
	if (end >= scroll->position + height) scroll->position = end - height;
	else if (start <= scroll->position) scroll->position = start;
	else unchanged = true;
	if (!unchanged) UIElementRefresh(w->element->parent);
}

void WatchAddExpression(WatchWindow *w, char *string = nullptr) {
	if (!string && w->textbox && !w->textbox->bytes) {
		WatchDestroyTextbox(w);
		return;
	}

	Watch *watch = (Watch *) calloc(1, sizeof(Watch));

	if (string) {
		watch->key = string;
	} else {
		watch->key = (char *) malloc(w->textbox->bytes + 1);
		watch->key[w->textbox->bytes] = 0;
		memcpy(watch->key, w->textbox->string, w->textbox->bytes);
	}

	WatchDeleteExpression(w); // Deletes textbox.
	w->rows.Insert(watch, w->selectedRow);
	w->baseExpressions.Add(watch);
	w->selectedRow++;

	WatchEvaluate("gf_typeof", watch);

	if (!strstr(evaluateResult, "??")) {
		watch->type = strdup(evaluateResult);
		char *end = strchr(watch->type, '\n');
		if (end) *end = 0;
		watch->hasFields = WatchHasFields(watch);
	}
}

int WatchLoggerWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		if (element->cp) {
			WatchLogger *logger = (WatchLogger *) element->cp;

			for (int i = 0; i < watchLoggers.Length(); i++) {
				if (watchLoggers[i] == logger) {
					watchLoggers.Delete(i);
					break;
				}
			}

			char buffer[256];
			StringFormat(buffer, sizeof(buffer), "delete %d", logger->id);
			EvaluateCommand(buffer);

			logger->entries.Free();
			free(logger);
		}
	} else if (message == UI_MSG_GET_WIDTH || message == UI_MSG_GET_HEIGHT) {
		return element->window->scale * 200;
	}

	return 0;
}

int WatchLoggerTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		WatchLogEntry *entry = &((WatchLogger *) element->cp)->entries[m->index];

		if (m->column == 0) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->value);
		} else if (m->column == 1) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->where);
		}
	} else if (message == UI_MSG_LAYOUT) {
		UITable *table = (UITable *) element;
		UI_FREE(table->columnWidths);
		table->columnCount = 2;
		table->columnWidths = (int *) UI_MALLOC(table->columnCount * sizeof(int));
		int available = UI_RECT_WIDTH(table->e.bounds) - UI_SIZE_SCROLL_BAR * element->window->scale;
		table->columnWidths[0] = available / 3;
		table->columnWidths[1] = 2 * available / 3;
	}

	return 0;
}

void WatchChangeLoggerCreate(WatchWindow *w) {
	// TODO Using the correct variable size.
	// TODO Make the MDI child a reasonable width/height by default.

	if (w->selectedRow == w->rows.Length()) {
		return;
	}

	if (!dataTab) {
		UIDialogShow(windowMain, 0, "The data window is not open.\nThe watch log cannot be created.\n%f%b", "OK");
		return;
	}

	WatchEvaluate("gf_addressof", w->rows[w->selectedRow]);

	if (strstr(evaluateResult, "??")) {
		UIDialogShow(windowMain, 0, "Couldn't get the address of the variable.\n%f%b", "OK");
		return;
	}

	char *end = strstr(evaluateResult, " ");

	if (!end) {
		UIDialogShow(windowMain, 0, "Couldn't get the address of the variable.\n%f%b", "OK");
		return;
	}

	*end = 0;
	char buffer[256];
	StringFormat(buffer, sizeof(buffer), "Log %s", evaluateResult);
	UIMDIChild *child = UIMDIChildCreate(&dataWindow->e, UI_MDI_CHILD_CLOSE_BUTTON, UI_RECT_1(0), buffer, -1);
	UITable *table = UITableCreate(&child->e, 0, "New value\tWhere");
	StringFormat(buffer, sizeof(buffer), "watch * %s", evaluateResult);
	EvaluateCommand(buffer);
	char *number = strstr(evaluateResult, "point ");

	if (!number) {
		UIDialogShow(windowMain, 0, "Couldn't set the watchpoint.\n%f%b", "OK");
		return;
	}

	number += 6;

	WatchLogger *logger = (WatchLogger *) calloc(1, sizeof(WatchLogger));
	logger->id = atoi(number);
	logger->table = table;
	child->e.cp = logger;
	table->e.cp = logger;
	child->e.messageUser = WatchLoggerWindowMessage;
	table->e.messageUser = WatchLoggerTableMessage;
	watchLoggers.Add(logger);
	UIElementRefresh(&dataWindow->e);

	UIDialogShow(windowMain, 0, "The log has been setup in the data window.\n%f%b", "OK");
	return;
}

bool WatchLoggerUpdate(char *data) {
	char *stringWatchpoint = strstr(data, "watchpoint ");
	if (!stringWatchpoint) return false;
	char *stringAddressStart = strstr(data, ": * ");
	if (!stringAddressStart) return false;
	int id = atoi(stringWatchpoint + 11);
	char *value = strstr(data, "\nNew value = ");
	if (!value) return false;
	value += 13;
	char *afterValue = strchr(value, '\n');
	if (!afterValue) return false;
	char *where = strstr(afterValue, " at ");
	if (!where) return false;
	where += 4;
	char *afterWhere = strchr(where, '\n');
	if (!afterWhere) return false;

	for (int i = 0; i < watchLoggers.Length(); i++) {
		if (watchLoggers[i]->id == id) {
			*afterValue = 0;
			*afterWhere = 0;
			WatchLogEntry entry = {};
			if (strlen(value) >= sizeof(entry.value)) value[sizeof(entry.value) - 1] = 0;
			if (strlen(where) >= sizeof(entry.where)) where[sizeof(entry.where) - 1] = 0;
			strcpy(entry.value, value);
			strcpy(entry.where, where);
			watchLoggers[i]->entries.Add(entry);
			watchLoggers[i]->table->itemCount++;
			UIElementRefresh(&watchLoggers[i]->table->e);
			DebuggerSend("c", false, false);
			return true;
		}
	}

	return false;
}

void WatchCreateTextboxForRow(WatchWindow *w, bool addExistingText) {
	int rowHeight = (int) (UI_SIZE_TEXTBOX_HEIGHT * w->element->window->scale);
	UIRectangle row = w->element->bounds;
	row.t += w->selectedRow * rowHeight, row.b = row.t + rowHeight;
	w->textbox = UITextboxCreate(w->element, 0);
	w->textbox->e.messageUser = WatchTextboxMessage;
	w->textbox->e.cp = w;
	UIElementMove(&w->textbox->e, row, true);
	UIElementFocus(&w->textbox->e);

	if (addExistingText) {
		UITextboxReplace(w->textbox, w->rows[w->selectedRow]->key, -1, false);
	}
}

int WatchWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	WatchWindow *w = (WatchWindow *) element->cp;
	int rowHeight = (int) (UI_SIZE_TEXTBOX_HEIGHT * element->window->scale);
	int result = 0;

	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;

		for (int i = (painter->clip.t - element->bounds.t) / rowHeight; i <= w->rows.Length(); i++) {
			UIRectangle row = element->bounds;
			row.t += i * rowHeight, row.b = row.t + rowHeight;

			UIRectangle intersection = UIRectangleIntersection(row, painter->clip);
			if (!UI_RECT_VALID(intersection)) break;

			bool focused = i == w->selectedRow && element->window->focused == element;

			if (focused) UIDrawBlock(painter, row, ui.theme.tableSelected);
			UIDrawBorder(painter, row, ui.theme.border, UI_RECT_4(1, 1, 0, 1));

			row.l += UI_SIZE_TEXTBOX_MARGIN;
			row.r -= UI_SIZE_TEXTBOX_MARGIN;

			if (i != w->rows.Length()) {
				Watch *watch = w->rows[i];
				char buffer[256];

				if ((!watch->value || watch->updateIndex != w->updateIndex) && !watch->open) {
					free(watch->value);
					watch->updateIndex = w->updateIndex;
					WatchEvaluate("gf_valueof", watch);
					watch->value = strdup(evaluateResult);
					char *end = strchr(watch->value, '\n');
					if (end) *end = 0;
				}

				char keyIndex[64];

				if (!watch->key) {
					StringFormat(keyIndex, sizeof(keyIndex), "[%lu]", watch->arrayIndex);
				}

				if (focused && w->waitingForFormatCharacter) {
					StringFormat(buffer, sizeof(buffer), "Enter format character: (e.g. 'x' for hex)");
				} else {
					StringFormat(buffer, sizeof(buffer), "%.*s%s%s%s%s", 
							watch->depth * 2, "                                ",
							watch->open ? "v " : watch->hasFields ? "> " : "", 
							watch->key ?: keyIndex, 
							watch->open ? "" : " = ", 
							watch->open ? "" : watch->value);
				}

				if (focused) {
					UIDrawString(painter, row, buffer, -1, ui.theme.tableSelectedText, UI_ALIGN_LEFT, nullptr);
				} else {
					UIDrawStringHighlighted(painter, row, buffer, -1, 1);
				}
			}
		}
	} else if (message == UI_MSG_GET_HEIGHT) {
		return (w->rows.Length() + 1) * rowHeight;
	} else if (message == UI_MSG_LEFT_DOWN) {
		w->selectedRow = (element->window->cursorY - element->bounds.t) / rowHeight;
		UIElementFocus(element);
		UIElementRepaint(element, nullptr);
	} else if (message == UI_MSG_RIGHT_DOWN) {
		int index = (element->window->cursorY - element->bounds.t) / rowHeight;

		if (index >= 0 && index < w->rows.Length()) {
			WatchWindowMessage(element, UI_MSG_LEFT_DOWN, di, dp);
			UIMenu *menu = UIMenuCreate(&element->window->e, 0);

			if (!w->rows[index]->parent) {
				UIMenuAddItem(menu, 0, "Edit expression", -1, [] (void *cp) { 
					WatchCreateTextboxForRow((WatchWindow *) cp, true); 
				}, w);

				UIMenuAddItem(menu, 0, "Delete", -1, [] (void *cp) { 
					WatchWindow *w = (WatchWindow *) cp;
					WatchDeleteExpression(w); 
					UIElementRefresh(w->element->parent);
					UIElementRefresh(w->element);
				}, w);
			}

			UIMenuAddItem(menu, 0, "Log changes", -1, [] (void *cp) { 
				WatchChangeLoggerCreate((WatchWindow *) cp); 
			}, w);

			UIMenuShow(menu);
		}
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, nullptr);
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;
		result = 1;

		if (w->waitingForFormatCharacter) {
			w->rows[w->selectedRow]->format = (m->textBytes && isalpha(m->text[0])) ? m->text[0] : 0;
			w->rows[w->selectedRow]->updateIndex--;
			w->waitingForFormatCharacter = false;
		} else if ((m->code == UI_KEYCODE_ENTER || m->code == UI_KEYCODE_BACKSPACE) 
				&& w->selectedRow != w->rows.Length() && !w->textbox
				&& !w->rows[w->selectedRow]->parent) {
			WatchCreateTextboxForRow(w, true);
		} else if (m->code == UI_KEYCODE_DELETE && !w->textbox
				&& w->selectedRow != w->rows.Length() && !w->rows[w->selectedRow]->parent) {
			WatchDeleteExpression(w);
		} else if (m->textBytes && m->text[0] == '/' && w->selectedRow != w->rows.Length()) {
			w->waitingForFormatCharacter = true;
		} else if (m->textBytes && m->code != UI_KEYCODE_TAB && !w->textbox && !element->window->ctrl && !element->window->alt
				&& (w->selectedRow == w->rows.Length() || !w->rows[w->selectedRow]->parent)) {
			WatchCreateTextboxForRow(w, false);
			UIElementMessage(&w->textbox->e, message, di, dp);
		} else if (m->code == UI_KEYCODE_ENTER && w->textbox) {
			WatchAddExpression(w);
		} else if (m->code == UI_KEYCODE_ESCAPE) {
			WatchDestroyTextbox(w);
		} else if (m->code == UI_KEYCODE_UP) {
			WatchDestroyTextbox(w);
			w->selectedRow--;
		} else if (m->code == UI_KEYCODE_DOWN) {
			WatchDestroyTextbox(w);
			w->selectedRow++;
		} else if (m->code == UI_KEYCODE_HOME) {
			w->selectedRow = 0;
		} else if (m->code == UI_KEYCODE_END) {
			w->selectedRow = w->rows.Length();
		} else if (m->code == UI_KEYCODE_RIGHT && !w->textbox
				&& w->selectedRow != w->rows.Length() && w->rows[w->selectedRow]->hasFields
				&& !w->rows[w->selectedRow]->open) {
			Watch *watch = w->rows[w->selectedRow];
			watch->open = true;
			WatchAddFields(watch);
			int position = w->selectedRow + 1;
			WatchInsertFieldRows(w, watch, &position);
			WatchEnsureRowVisible(w, position - 1);
		} else if (m->code == UI_KEYCODE_LEFT && !w->textbox
				&& w->selectedRow != w->rows.Length() && w->rows[w->selectedRow]->hasFields
				&& w->rows[w->selectedRow]->open) {
			int end = w->selectedRow + 1;

			for (; end < w->rows.Length(); end++) {
				if (w->rows[w->selectedRow]->depth >= w->rows[end]->depth) {
					break;
				}
			}

			w->rows.Delete(w->selectedRow + 1, end - w->selectedRow - 1);
			w->rows[w->selectedRow]->open = false;
		} else if (m->code == UI_KEYCODE_LEFT && !w->textbox 
				&& w->selectedRow != w->rows.Length() && !w->rows[w->selectedRow]->open) {
			for (int i = 0; i < w->rows.Length(); i++) {
				if (w->rows[w->selectedRow]->parent == w->rows[i]) {
					w->selectedRow = i;
					break;
				}
			}
		} else {
			result = 0;
		}

		WatchEnsureRowVisible(w, w->selectedRow);
		UIElementRefresh(element->parent);
		UIElementRefresh(element);
	}

	if (w->selectedRow < 0) {
		w->selectedRow = 0;
	} else if (w->selectedRow > w->rows.Length()) {
		w->selectedRow = w->rows.Length();
	}

	return result;
}

int WatchPanelMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_LEFT_DOWN) {
		UIElement *window = ((WatchWindow *) element->cp)->element;
		UIElementFocus(window);
		UIElementRepaint(window, nullptr);
	}

	return 0;
}

UIElement *WatchWindowCreate(UIElement *parent) {
	WatchWindow *w = (WatchWindow *) calloc(1, sizeof(WatchWindow));
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_SCROLL | UI_PANEL_GRAY);
	panel->e.messageUser = WatchPanelMessage;
	panel->e.cp = w;
	w->element = UIElementCreate(sizeof(UIElement), &panel->e, UI_ELEMENT_H_FILL | UI_ELEMENT_TAB_STOP, WatchWindowMessage, "Watch");
	w->element->cp = w;
	return &panel->e;
}

void WatchWindowUpdate(const char *, UIElement *element) {
	WatchWindow *w = (WatchWindow *) element->cp;

	for (int i = 0; i < w->baseExpressions.Length(); i++) {
		Watch *watch = w->baseExpressions[i];
		WatchEvaluate("gf_typeof", watch);
		char *result = strdup(evaluateResult);
		char *end = strchr(result, '\n');
		if (end) *end = 0;
		const char *oldType = watch->type ?: "??";

		if (strcmp(result, oldType)) {
			free(watch->type);
			watch->type = result;

			for (int j = 0; j < w->rows.Length(); j++) {
				if (w->rows[j] == watch) {
					w->selectedRow = j;
					WatchAddExpression(w, strdup(watch->key));
					w->selectedRow = w->rows.Length(), i--;
					break;
				}
			}
		} else {
			free(result);
		}
	}

	w->updateIndex++;
	UIElementRefresh(element->parent);
	UIElementRefresh(element);
}

void WatchWindowFocus(UIElement *element) {
	WatchWindow *w = (WatchWindow *) element->cp;
	UIElementFocus(w->element);
}

//////////////////////////////////////////////////////
// Stack window:
//////////////////////////////////////////////////////

int TableStackMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		m->isSelected = m->index == stackSelected;
		StackEntry *entry = &stack[m->index];

		if (m->column == 0) {
			return StringFormat(m->buffer, m->bufferBytes, "%d", entry->id);
		} else if (m->column == 1) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->function);
		} else if (m->column == 2) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->location);
		} else if (m->column == 3) {
			return StringFormat(m->buffer, m->bufferBytes, "0x%lX", entry->address);
		}
	} else if (message == UI_MSG_LEFT_DOWN || message == UI_MSG_MOUSE_DRAG) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);

		if (index != -1 && stackSelected != index) {
			char buffer[64];
			StringFormat(buffer, 64, "frame %d", index);
			DebuggerSend(buffer, false, false);
			stackSelected = index;
			stackChanged = true;
			UIElementRepaint(element, nullptr);
		}
	}

	return 0;
}

UIElement *StackWindowCreate(UIElement *parent) {
	UITable *table = UITableCreate(parent, 0, "Index\tFunction\tLocation\tAddress");
	table->e.messageUser = TableStackMessage;
	return &table->e;
}

void StackWindowUpdate(const char *, UIElement *_table) {
	UITable *table = (UITable *) _table;
	table->itemCount = stack.Length();
	UITableResizeColumns(table);
	UIElementRefresh(&table->e);
}

//////////////////////////////////////////////////////
// Breakpoints window:
//////////////////////////////////////////////////////

int TableBreakpointsMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		Breakpoint *entry = &breakpoints[m->index];

		if (m->column == 0) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->file);
		} else if (m->column == 1) {
			return StringFormat(m->buffer, m->bufferBytes, "%d", entry->line);
		}
	} else if (message == UI_MSG_RIGHT_DOWN) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);

		if (index != -1) {
			UIMenu *menu = UIMenuCreate(&element->window->e, 0);
			UIMenuAddItem(menu, 0, "Delete", -1, CommandDeleteBreakpoint, (void *) (intptr_t) index);
			UIMenuShow(menu);
		}
	} else if (message == UI_MSG_LEFT_DOWN) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);
		if (index != -1) DisplaySetPosition(breakpoints[index].file, breakpoints[index].line, false);
	}

	return 0;
}

UIElement *BreakpointsWindowCreate(UIElement *parent) {
	UITable *table = UITableCreate(parent, 0, "File\tLine");
	table->e.messageUser = TableBreakpointsMessage;
	return &table->e;
}

void BreakpointsWindowUpdate(const char *, UIElement *_table) {
	UITable *table = (UITable *) _table;
	table->itemCount = breakpoints.Length();
	UITableResizeColumns(table);
	UIElementRefresh(&table->e);
}

//////////////////////////////////////////////////////
// Data window:
//////////////////////////////////////////////////////

UIButton *buttonFillWindow;

int DataTabMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TAB_SELECTED && autoUpdateBitmapViewersQueued) {
		// If we've switched to the data tab, we may need to update the bitmap viewers.

		for (int i = 0; i < autoUpdateBitmapViewers.Length(); i++) {
			BitmapViewer *bitmap = (BitmapViewer *) autoUpdateBitmapViewers[i]->cp;
			BitmapViewerUpdate(bitmap->pointer, bitmap->width, bitmap->height, bitmap->stride, autoUpdateBitmapViewers[i]);
		}

		autoUpdateBitmapViewersQueued = false;
	}

	return 0;
}

void CommandToggleFillDataTab(void *) {
	// HACK.

	if (!dataTab) return;
	static UIElement *oldParent;
	UIWindow *window = dataTab->e.window;
	
	if (window->e.children == &dataTab->e) {
		UIElementChangeParent(&dataTab->e, oldParent, false);
		buttonFillWindow->e.flags &= ~UI_BUTTON_CHECKED;
		UIElementRefresh(&window->e);
		UIElementRefresh(window->e.children);
		UIElementRefresh(oldParent);
	} else {
		dataTab->e.flags &= ~UI_ELEMENT_HIDE;
		UIElementMessage(&dataTab->e, UI_MSG_TAB_SELECTED, 0, 0);
		oldParent = dataTab->e.parent;
		window->e.children->clip = UI_RECT_1(0);
		UIElementChangeParent(&dataTab->e, &window->e, true);
		buttonFillWindow->e.flags |= UI_BUTTON_CHECKED;
		UIElementRefresh(&window->e);
		UIElementRefresh(&dataTab->e);
	}
}

UIElement *DataWindowCreate(UIElement *parent) {
	dataTab = UIPanelCreate(parent, UI_PANEL_EXPAND);
	UIPanel *panel5 = UIPanelCreate(&dataTab->e, UI_PANEL_GRAY | UI_PANEL_HORIZONTAL | UI_PANEL_SMALL_SPACING);
	buttonFillWindow = UIButtonCreate(&panel5->e, UI_BUTTON_SMALL, "Fill window", -1);
	buttonFillWindow->invoke = CommandToggleFillDataTab;
	UIButtonCreate(&panel5->e, UI_BUTTON_SMALL, "Add bitmap...", -1)->invoke = BitmapAddDialog;
	dataWindow = UIMDIClientCreate(&dataTab->e, UI_ELEMENT_V_FILL);
	dataTab->e.messageUser = DataTabMessage;
	return &dataTab->e;
}

//////////////////////////////////////////////////////
// Struct window:
//////////////////////////////////////////////////////

struct StructWindow {
	UICode *display;
	UITextbox *textbox;
};

int TextboxStructNameMessage(UIElement *element, UIMessage message, int di, void *dp) {
	StructWindow *window = (StructWindow *) element->cp;

	if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		if (m->code == UI_KEYCODE_ENTER) {
			char buffer[4096];
			StringFormat(buffer, sizeof(buffer), "ptype /o %.*s", (int) window->textbox->bytes, window->textbox->string);
			EvaluateCommand(buffer);
			char *end = strstr(evaluateResult, "\n(gdb)");
			if (end) *end = 0;
			UICodeInsertContent(window->display, evaluateResult, -1, true);
			UITextboxClear(window->textbox, false);
			UIElementRefresh(&window->display->e);
			UIElementRefresh(element);
			return 1;
		}
	}

	return 0;
}

UIElement *StructWindowCreate(UIElement *parent) {
	StructWindow *window = (StructWindow *) calloc(1, sizeof(StructWindow));
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_GRAY | UI_PANEL_EXPAND);
	window->textbox = UITextboxCreate(&panel->e, 0);
	window->textbox->e.messageUser = TextboxStructNameMessage;
	window->textbox->e.cp = window;
	window->display = UICodeCreate(&panel->e, UI_ELEMENT_V_FILL | UI_CODE_NO_MARGIN);
	UICodeInsertContent(window->display, "Type the name of a struct\nto view its layout.", -1, false);
	return &panel->e;
}

//////////////////////////////////////////////////////
// Files window:
//////////////////////////////////////////////////////

struct FilesWindow {
	char directory[PATH_MAX];
	UIPanel *panel;
};

bool FilesPanelPopulate(FilesWindow *window);

int FilesButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		FilesWindow *window = (FilesWindow *) element->cp;
		const char *name = ((UIButton *) element)->label;
		size_t oldLength = strlen(window->directory);
		strcat(window->directory, "/");
		strcat(window->directory, name);
		struct stat s;
		stat(window->directory, &s);

		if (S_ISDIR(s.st_mode)) {
			if (FilesPanelPopulate(window)) {
				char copy[PATH_MAX];
				realpath(window->directory, copy);
				strcpy(window->directory, copy);
				return 0;
			}
		} else if (S_ISREG(s.st_mode)) {
			DisplaySetPosition(window->directory, 1, false);
		}

		window->directory[oldLength] = 0;
	}

	return 0;
}

bool FilesPanelPopulate(FilesWindow *window) {
	DIR *directory = opendir(window->directory);
	struct dirent *entry;
	if (!directory) return false;
	Array<char *> names = {};
	while ((entry = readdir(directory))) names.Add(strdup(entry->d_name));
	closedir(directory);
	UIElementDestroyDescendents(&window->panel->e);

	qsort(names.array, names.Length(), sizeof(char *), [] (const void *a, const void *b) {
		return strcmp(*(const char **) a, *(const char **) b);
	});

	for (int i = 0; i < names.Length(); i++) {
		if (names[i][0] != '.' || names[i][1] != 0) {
			UIButton *button = UIButtonCreate(&window->panel->e, 0, names[i], -1);
			button->e.cp = window;
			button->e.messageUser = FilesButtonMessage;
		}

		free(names[i]);
	}

	names.Free();
	UIElementRefresh(&window->panel->e);
	return true;
}

UIElement *FilesWindowCreate(UIElement *parent) {
	FilesWindow *window = (FilesWindow *) calloc(1, sizeof(FilesWindow));
	window->panel = UIPanelCreate(parent, UI_PANEL_GRAY | UI_PANEL_EXPAND | UI_PANEL_SCROLL);
	window->panel->e.cp = window;
	getcwd(window->directory, sizeof(window->directory));
	FilesPanelPopulate(window);
	return &window->panel->e;
}

//////////////////////////////////////////////////////
// Registers window:
//////////////////////////////////////////////////////

struct RegisterData { char string[128]; };
Array<RegisterData> registerData;

UIElement *RegistersWindowCreate(UIElement *parent) {
	return &UIPanelCreate(parent, UI_PANEL_SMALL_SPACING | UI_PANEL_GRAY | UI_PANEL_SCROLL)->e;
}

void RegistersWindowUpdate(const char *, UIElement *panel) {
	EvaluateCommand("info registers");

	if (strstr(evaluateResult, "The program has no registers now.")
			|| strstr(evaluateResult, "The current thread has terminated")) {
		return;
	}

	UIElementDestroyDescendents(panel);
	char *position = evaluateResult;
	Array<RegisterData> newRegisterData = {};
	bool anyChanges = false;

	while (*position != '(') {
		char *nameStart = position;
		while (isspace(*nameStart)) nameStart++;
		char *nameEnd = position = strchr(nameStart, ' ');
		if (!nameEnd) break;
		char *format1Start = position;
		while (isspace(*format1Start)) format1Start++;
		char *format1End = position = strchr(format1Start, ' ');
		if (!format1End) break;
		char *format2Start = position;
		while (isspace(*format2Start)) format2Start++;
		char *format2End = position = strchr(format2Start, '\n');
		if (!format2End) break;

		char *stringStart = nameStart;
		char *stringEnd = format2End;

		RegisterData data;
		StringFormat(data.string, sizeof(data.string), "%.*s",
				(int) (stringEnd - stringStart), stringStart);
		bool modified = false;

		if (registerData.Length() > newRegisterData.Length()) {
			RegisterData *old = &registerData[newRegisterData.Length()];

			if (strcmp(old->string, data.string)) {
				modified = true;
			}
		}

		newRegisterData.Add(data);

		UIPanel *row = UIPanelCreate(panel, UI_PANEL_HORIZONTAL | UI_ELEMENT_H_FILL);
		if (modified) row->e.messageUser = ModifiedRowMessage;
		UILabelCreate(&row->e, 0, stringStart, stringEnd - stringStart);

		bool isPC = false;
		if (nameEnd == nameStart + 3 && 0 == memcmp(nameStart, "rip", 3)) isPC = true;
		if (nameEnd == nameStart + 3 && 0 == memcmp(nameStart, "eip", 3)) isPC = true;
		if (nameEnd == nameStart + 2 && 0 == memcmp(nameStart,  "ip", 2)) isPC = true;

		if (modified && showingDisassembly && !isPC) {
			if (!anyChanges) {
				autoPrintResult[0] = 0;
				autoPrintResultLine = autoPrintExpressionLine;
				anyChanges = true;
			} else {
				int position = strlen(autoPrintResult);
				StringFormat(autoPrintResult + position, sizeof(autoPrintResult) - position, ", ");
			}

			int position = strlen(autoPrintResult);
			StringFormat(autoPrintResult + position, sizeof(autoPrintResult) - position, "%.*s=%.*s",
					(int) (nameEnd - nameStart), nameStart,
					(int) (format1End - format1Start), format1Start);
		}
	}

	UIElementRefresh(panel);
	registerData.Free();
	registerData = newRegisterData;
}

//////////////////////////////////////////////////////
// Commands window:
//////////////////////////////////////////////////////

UIElement *CommandsWindowCreate(UIElement *parent) {
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_GRAY | UI_PANEL_SMALL_SPACING | UI_PANEL_EXPAND | UI_PANEL_SCROLL);
	if (!presetCommands.Length()) UILabelCreate(&panel->e, 0, "No preset commands found in config file!", -1);

	for (int i = 0; i < presetCommands.Length(); i++) {
		char buffer[256];
		StringFormat(buffer, sizeof(buffer), "gf-command %s", presetCommands[i].key);
		UIButton *button = UIButtonCreate(&panel->e, 0, presetCommands[i].key, -1);
		button->e.cp = strdup(buffer);
		button->invoke = CommandSendToGDB;
	}

	return &panel->e;
}

//////////////////////////////////////////////////////
// Log window:
//////////////////////////////////////////////////////

void *LogWindowThread(void *context) {
	if (!logPipePath) { fprintf(stderr, "Error: The log pipe path has not been set in the configuration file!\n"); return nullptr; }
	int file = open(logPipePath, O_RDONLY | O_NONBLOCK);
	if (file == -1) { fprintf(stderr, "Error: Could not open the log pipe!\n"); return nullptr; }

	struct pollfd p = { .fd = file, .events = POLLIN };

	while (true) {
		poll(&p, 1, 10000);

		while (true) {
			char input[16384];
			int length = read(file, input, sizeof(input) - 1);
			if (length <= 0) break;
			input[length] = 0;
			void *buffer = malloc(strlen(input) + sizeof(context) + 1);
			memcpy(buffer, &context, sizeof(context));
			strcpy((char *) buffer + sizeof(context), input);
			UIWindowPostMessage(windowMain, MSG_RECEIVED_LOG, buffer);
		}
	}
}

void LogReceived(void *buffer) {
	UICodeInsertContent(*(UICode **) buffer, (char *) buffer + sizeof(void *), -1, false);
	UIElementRefresh(*(UIElement **) buffer);
	free(buffer);
}

UIElement *LogWindowCreate(UIElement *parent) {
	UICode *code = UICodeCreate(parent, 0);
	pthread_t thread;
	pthread_create(&thread, nullptr, LogWindowThread, code);
	return &code->e;
}

//////////////////////////////////////////////////////
// Thread window:
//////////////////////////////////////////////////////

struct Thread {
	char frame[127];
	bool active;
};

struct ThreadWindow {
	Array<Thread> threads;
};

int ThreadTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	ThreadWindow *window = (ThreadWindow *) element->cp;

	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		m->isSelected = window->threads[m->index].active;

		if (m->column == 0) {
			return StringFormat(m->buffer, m->bufferBytes, "%d", m->index + 1);
		} else if (m->column == 1) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", window->threads[m->index].frame);
		}
	} else if (message == UI_MSG_LEFT_DOWN) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);

		if (index != -1) {
			char buffer[1024];
			StringFormat(buffer, 1024, "thread %d", index + 1);
			DebuggerSend(buffer, true, false);
		}
	}

	return 0;
}

UIElement *ThreadWindowCreate(UIElement *parent) {
	UITable *table = UITableCreate(parent, 0, "ID\tFrame");
	table->e.cp = (ThreadWindow *) calloc(1, sizeof(ThreadWindow));
	table->e.messageUser = ThreadTableMessage;
	return &table->e;
}

void ThreadWindowUpdate(const char *, UIElement *_table) {
	ThreadWindow *window = (ThreadWindow *) _table->cp;
	window->threads.length = 0;

	EvaluateCommand("info threads");
	char *position = evaluateResult;

	for (int i = 0; position[i]; i++) {
		if (position[i] == '\n' && position[i + 1] == ' ' && position[i + 2] == ' ' && position[i + 3] == ' ') {
			memmove(position + i, position + i + 3, strlen(position) - 3 - i + 1);
		}
	}

	while (true) {
		position = strchr(position, '\n');
		if (!position) break;
		Thread thread = {};
		if (position[1] == '*') thread.active = true;
		position = strchr(position + 1, '"');
		if (!position) break;
		position = strchr(position + 1, '"');
		if (!position) break;
		position++;
		char *end = strchr(position, '\n');
		if (end - position >= (ptrdiff_t) sizeof(thread.frame)) 
			end = position + sizeof(thread.frame) - 1;
		memcpy(thread.frame, position, end - position);
		thread.frame[end - position] = 0;
		window->threads.Add(thread);
	}

	UITable *table = (UITable *) _table;
	table->itemCount = window->threads.Length();
	UITableResizeColumns(table);
	UIElementRefresh(&table->e);
}
