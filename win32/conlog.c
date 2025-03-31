/************************************
 * Copyright (c) 2025 Roger Brown.
 * Licensed under the MIT License.
 */

#include <windows.h>
#include <winerror.h>
#include <stdio.h>
#include <stdlib.h>

struct conlog_input
{
	DWORD mode;
	BOOL running, reportFocus, hasFocus, appFocus;
	HANDLE hRead, hWrite, hEvent, hControl, hScreen;
	HPCON hPC;
};

struct conlog_output_channel
{
	DWORD mode;
	int cp;
	BOOL bConsole;
	HANDLE hWrite;
};

struct conlog_output
{
	HANDLE hRead, hControl;
	int nChannels;
	struct conlog_output_channel channels[2];
	struct conlog_input* input;
	BYTE buffer[4096];
	DWORD bufferLength;
};

static void conlog_output_flush(struct conlog_output* state)
{
	if (state->bufferLength)
	{
		int nChannels = state->nChannels;
		struct conlog_output_channel* channel = state->channels;

		while (nChannels--)
		{
			const BYTE* p = state->buffer;
			DWORD len = state->bufferLength;

			while (len)
			{
				DWORD dw;
				BOOL bWrite;

				if (channel->bConsole)
				{
					bWrite = WriteConsoleA(channel->hWrite, p, len, &dw, NULL);
				}
				else
				{
					bWrite = WriteFile(channel->hWrite, p, len, &dw, NULL);
				}

				if (!bWrite) break;
				if (!dw) break;

				p += dw;
				len -= dw;
			}

			channel++;
		}

		state->bufferLength = 0;
	}
}

static void conlog_output_write(struct conlog_output* state, const BYTE* data, DWORD len)
{
	while (len)
	{
		DWORD n = sizeof(state->buffer) - state->bufferLength;

		if (n)
		{
			if (n > len)
			{
				n = len;
			}

			memcpy(state->buffer + state->bufferLength, data, n);

			data += n;
			len -= n;
			state->bufferLength += n;
		}
		else
		{
			conlog_output_flush(state);
		}
	}

	if (state->bufferLength == sizeof(state->buffer))
	{
		conlog_output_flush(state);
	}
}

static DWORD CALLBACK output_thread(LPVOID pv)
{
	struct conlog_output* state = pv;
	char buf[4096];
	DWORD dwRead;
	int colonCount = 0;
	int digitCount = 0;
	int escapeCommittee = 0;
	int args[5];
	char escapeRoom[128];
	int escapeLen = 0;

	while (ReadFile(state->hRead, buf, sizeof(buf), &dwRead, NULL))
	{
		const char* input = buf;
		DWORD offset = 0;

		if (dwRead == 0) break;

		while (offset < dwRead)
		{
			char c = input[offset];

			if (escapeLen)
			{
				if (escapeLen < sizeof(escapeRoom))
				{
					escapeRoom[escapeLen++] = c;
					offset++;

					switch (escapeCommittee)
					{
					case 1:
						if (c == '[')
						{
							colonCount = 0;
							digitCount = 0;
							escapeCommittee = 2;
							args[0] = 0;
						}
						else
						{
							conlog_output_write(state, escapeRoom, escapeLen);
							escapeCommittee = 0;
							escapeLen = 0;
							input += offset;
							dwRead -= offset;
							offset = 0;
						}
						break;

					case 2:
					case 3:
						switch (c)
						{
						case ';':
							if (colonCount < (sizeof(args) / sizeof(args[0])))
							{
								args[colonCount++] = 0;
								digitCount = 0;
							}
							break;

						case '?':
							if (escapeCommittee == 2 && colonCount == 0 && digitCount == 0)
							{
								escapeCommittee = 3;
							}
							else
							{
								conlog_output_write(state, escapeRoom, escapeLen);
								escapeCommittee = 0;
								escapeLen = 0;
								input += offset;
								dwRead -= offset;
								offset = 0;
							}
							break;

						default:
							if (isdigit(c))
							{
								if (colonCount < (sizeof(args) / sizeof(args[0])))
								{
									args[colonCount] = (args[colonCount] * 10) + (c - '0');
									digitCount++;
								}
							}
							else
							{
								switch (escapeCommittee)
								{
								case 3:
									switch (c)
									{
									case 'h':
										if (digitCount && (args[0] == 1004))
										{
											if (!state->input->reportFocus)
											{
												DWORD dw;
												state->input->appFocus = !state->input->hasFocus;
												state->input->reportFocus = TRUE;
												if (WriteFile(state->hControl, "\001", 1, &dw, NULL) && dw)
												{
													SetEvent(state->input->hEvent);
												}
											}
										}
										break;

									case 'l':
										if (digitCount && (args[0] == 1004))
										{
											state->input->reportFocus = FALSE;
										}
										break;
									}
									break;

								case 2:
									switch (c)
									{
									case 'n':
										if (digitCount && (args[0] == 6))
										{
											DWORD dw;

											conlog_output_flush(state);

											if (WriteFile(state->hControl, "\002", 1, &dw, NULL) && dw)
											{
												SetEvent(state->input->hEvent);
												escapeLen = 0;
											}
										}
										break;
									}
									break;
								}
								conlog_output_write(state, escapeRoom, escapeLen);
								escapeCommittee = 0;
								escapeLen = 0;
								input += offset;
								dwRead -= offset;
								offset = 0;
							}
							break;
						}
						break;

					default:
						break;
					}
				}
				else
				{
					conlog_output_write(state, escapeRoom, escapeLen);
					escapeCommittee = 0;
					escapeLen = 0;
					input += offset;
					dwRead -= offset;
					offset = 0;
				}
			}
			else
			{
				if (c == 27)
				{
					conlog_output_write(state, input, offset);

					escapeRoom[escapeLen++] = c;
					offset++;
					dwRead -= offset;
					input += offset;
					offset = 0;
					escapeCommittee = 1;
				}
				else
				{
					offset++;
				}
			}
		}

		conlog_output_write(state, input, offset);
		conlog_output_flush(state);
	}

	return 0;
}

static DWORD CALLBACK input_thread(LPVOID pv)
{
	struct conlog_input* state = pv;
	BOOL running = TRUE;

	while (state->running && running)
	{
		HANDLE hEvent[] = { state->hRead,state->hEvent };
		INPUT_RECORD input;
		DWORD dw = WaitForMultipleObjects(2, hEvent, FALSE, INFINITE);

		switch (dw)
		{
		case WAIT_OBJECT_0:
			running = ReadConsoleInput(state->hRead, &input, 1, &dw);

			if (running)
			{
				switch (input.EventType)
				{
				case WINDOW_BUFFER_SIZE_EVENT:
					ResizePseudoConsole(state->hPC, input.Event.WindowBufferSizeEvent.dwSize);
					break;

				case KEY_EVENT:
					if (input.Event.KeyEvent.bKeyDown)
					{
						char read_buffer[256];
						int read_len = 0;

						if (input.Event.KeyEvent.uChar.UnicodeChar)
						{
							wchar_t wide = input.Event.KeyEvent.uChar.UnicodeChar;

							switch (wide)
							{
							case ' ':
								if (input.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
								{
									read_buffer[read_len++] = 0;
								}
								else
								{
									read_buffer[read_len++] = ' ';
								}
								break;
							default:
								read_len = WideCharToMultiByte(CP_UTF8, 0, &wide, 1, read_buffer, sizeof(read_buffer), NULL, NULL);
								break;
							}
						}
						else
						{
							char code = 0, cis = '[';
							int argc = 0;
							int argv[10];
							int shift = 0;

							switch (input.Event.KeyEvent.dwControlKeyState & (SHIFT_PRESSED | LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED | LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
							{
							case SHIFT_PRESSED:
								shift = 2;
								break;
							case SHIFT_PRESSED | LEFT_ALT_PRESSED:
							case SHIFT_PRESSED | RIGHT_ALT_PRESSED:
								shift = 4;
								break;
							case LEFT_CTRL_PRESSED:
							case RIGHT_CTRL_PRESSED:
								shift = 5;
								break;
							case SHIFT_PRESSED | LEFT_CTRL_PRESSED:
							case SHIFT_PRESSED | RIGHT_CTRL_PRESSED:
								shift = 6;
								break;
							case LEFT_ALT_PRESSED | LEFT_CTRL_PRESSED:
							case LEFT_ALT_PRESSED | RIGHT_CTRL_PRESSED:
							case RIGHT_ALT_PRESSED | LEFT_CTRL_PRESSED:
							case RIGHT_ALT_PRESSED | RIGHT_CTRL_PRESSED:
								shift = 7;
								break;
							case SHIFT_PRESSED | LEFT_ALT_PRESSED | LEFT_CTRL_PRESSED:
							case SHIFT_PRESSED | LEFT_ALT_PRESSED | RIGHT_CTRL_PRESSED:
							case SHIFT_PRESSED | RIGHT_ALT_PRESSED | LEFT_CTRL_PRESSED:
							case SHIFT_PRESSED | RIGHT_ALT_PRESSED | RIGHT_CTRL_PRESSED:
								shift = 8;
								break;
							}

							switch (input.Event.KeyEvent.wVirtualKeyCode)
							{
							case VK_ESCAPE:
								code = 'P';
								break;
							case VK_UP:
								if (shift)
								{
									argv[argc++] = 1;
								}
								code = 'A';
								break;
							case VK_DOWN:
								if (shift)
								{
									argv[argc++] = 1;
								}
								code = 'B';
								break;
							case VK_RIGHT:
								if (shift)
								{
									argv[argc++] = 1;
								}
								code = 'C';
								break;
							case VK_LEFT:
								if (shift)
								{
									argv[argc++] = 1;
								}
								code = 'D';
								break;
							case VK_CLEAR:
								if (shift)
								{
									argv[argc++] = 1;
								}
								code = 'E';
								break;
							case VK_F1:
								if (shift)
								{
									argv[argc++] = 1;
								}
								else
								{
									cis = 'O';
								}
								code = 'P';
								break;
							case VK_F2:
								if (shift)
								{
									argv[argc++] = 1;
								}
								else
								{
									cis = 'O';
								}
								code = 'Q';
								break;
							case VK_F3:
								if (shift)
								{
									argv[argc++] = 1;
								}
								else
								{
									cis = 'O';
								}
								code = 'R';
								break;
							case VK_F4:
								if (shift)
								{
									argv[argc++] = 1;
								}
								else
								{
									cis = 'O';
								}
								code = 'S';
								break;
							case VK_HOME:
								code = '~'; argv[argc++] = 1;
								break;
							case VK_INSERT:
								code = '~'; argv[argc++] = 2;
								break;
							case VK_DELETE:
								code = '~'; argv[argc++] = 3;
								break;
							case VK_END:
								code = '~'; argv[argc++] = 4;
								break;
							case VK_PRIOR:
								code = '~'; argv[argc++] = 5;
								break;
							case VK_NEXT:
								code = '~'; argv[argc++] = 6;
								break;
							case VK_F5:
								code = '~'; argv[argc++] = 15;
								break;
							case VK_F6:
								code = '~'; argv[argc++] = 17;
								break;
							case VK_F7:
								code = '~'; argv[argc++] = 18;
								break;
							case VK_F8:
								code = '~'; argv[argc++] = 19;
								break;
							case VK_F9:
								code = '~'; argv[argc++] = 20;
								break;
							case VK_F10:
								code = '~'; argv[argc++] = 21;
								break;
							case VK_F11:
								code = '~'; argv[argc++] = 23;
								break;
							case VK_F12:
								code = '~'; argv[argc++] = 24;
								break;
							}

							if (code)
							{
								int i = 0;
								read_buffer[read_len++] = 27;
								read_buffer[read_len++] = cis;

								if (shift && argc)
								{
									argv[argc++] = shift;
								}

								while (i < argc)
								{
									if (i)
									{
										read_buffer[read_len++] = ';';
									}

									read_len += sprintf_s(read_buffer + read_len, sizeof(read_buffer) - read_len, "%d", argv[i++]);
								}

								read_buffer[read_len++] = code;
							}
						}

						if (read_len)
						{
							running = WriteFile(state->hWrite, read_buffer, read_len, &dw, NULL) && (dw == read_len);
						}
					}

					break;

				case FOCUS_EVENT:
					state->hasFocus = input.Event.FocusEvent.bSetFocus;

					if (state->reportFocus && (state->hasFocus != state->appFocus))
					{
						state->appFocus = state->hasFocus;

						WriteFile(state->hWrite, input.Event.FocusEvent.bSetFocus ? "\033[I" : "\033[O", 3, &dw, NULL);
					}

					break;

				default:
					break;
				}
			}

			break;

		case WAIT_OBJECT_0 + 1:
			while (running && state->running)
			{
				BYTE buf[1] = { 0 };
				DWORD totalBytesAvailable = 0, bytesLeftInThisMessage = 0;

				running = PeekNamedPipe(state->hControl, NULL, 0, &dw, &totalBytesAvailable, &bytesLeftInThisMessage);

				if (running && totalBytesAvailable)
				{
					running = ReadFile(state->hControl, buf, 1, &dw, NULL) && (dw == 1);

					if (running)
					{
						CONSOLE_SCREEN_BUFFER_INFO screen;

						switch (buf[0])
						{
						case 0:
							running = FALSE;
							break;

						case 1:
							if (state->reportFocus && (state->hasFocus != state->appFocus))
							{
								state->appFocus = state->hasFocus;
								running = WriteFile(state->hWrite, state->hasFocus ? "\033[I" : "\033[O", 3, &dw, NULL);
							}
							break;

						case 2:
							running = GetConsoleScreenBufferInfo(state->hScreen, &screen);

							if (running)
							{
								char response[32];
								int i = sprintf_s(response, sizeof(response), "\033[%d;%dR", screen.dwCursorPosition.Y + 1, screen.dwCursorPosition.X + 1);
								running = WriteFile(state->hWrite, response, i, &dw, NULL);
							}

							break;
						}
					}
				}
				else
				{
					break;
				}
			}
			break;

		default:
			running = FALSE;
			break;
		}
	}

	return 0;
}

int main(int argc, char** argv)
{
	int cp = GetACP();
	const wchar_t* cmdLine = GetCommandLineW();
	struct conlog_output output;
	struct conlog_input input;
	wchar_t comspec[260];
	DWORD tidInput = 0, tidOutput = 0;
	HANDLE threadInput = 0, threadOutput = 0;
	int exitCode = ERROR_INVALID_FUNCTION;
	CONSOLE_SCREEN_BUFFER_INFO info;
	int nChannels;
	struct conlog_output_channel* channel = output.channels;
	BOOL bHaveConsole = FALSE, bWriteError = TRUE;

	SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

	ZeroMemory(&info, sizeof(info));
	ZeroMemory(&output, sizeof(output));
	ZeroMemory(&input, sizeof(input));

	output.input = &input;

	if (!CreatePipe(&input.hControl, &output.hControl, NULL, 0))
	{
		exitCode = GetLastError();

		fprintf(stderr, "Failed to create internal pipe\n");
		fflush(stderr);

		return exitCode;
	}

	input.hRead = GetStdHandle(STD_INPUT_HANDLE);

	if (!GetConsoleMode(input.hRead, &input.mode))
	{
		exitCode = GetLastError();

		fprintf(stderr, "Input is not a console\n");
		fflush(stderr);

		return exitCode;
	}

	if (!SetConsoleMode(input.hRead, (ENABLE_WINDOW_INPUT | input.mode) & (~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT))))
	{
		exitCode = GetLastError();

		fprintf(stderr, "Input does not support required mode\n");
		fflush(stderr);

		return exitCode;
	}

	input.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	output.channels[0].cp = CP_UTF8;
	output.channels[0].hWrite = GetStdHandle(STD_OUTPUT_HANDLE);
	output.channels[0].bConsole = GetConsoleMode(output.channels[0].hWrite, &output.channels[0].mode);

	output.channels[1].cp = CP_UTF8;
	output.channels[1].hWrite = GetStdHandle(STD_ERROR_HANDLE);
	output.channels[1].bConsole = GetConsoleMode(output.channels[1].hWrite, &output.channels[1].mode);

	if (output.channels[0].bConsole && output.channels[1].bConsole)
	{
		SetConsoleMode(input.hRead, input.mode);

		fprintf(stderr, "Both stdout and stderr are console devices\n");
		fflush(stderr);

		return ERROR_NOT_SUPPORTED;
	}

	if (!(output.channels[0].bConsole || output.channels[1].bConsole))
	{
		SetConsoleMode(input.hRead, input.mode);

		fprintf(stderr, "No console output\n");
		fflush(stderr);

		return ERROR_NOT_SUPPORTED;
	}

	output.nChannels = 2;

	if (output.channels[1].bConsole)
	{
		SetStdHandle(STD_OUTPUT_HANDLE, output.channels[1].hWrite);
	}
	else
	{
		SetStdHandle(STD_ERROR_HANDLE, output.channels[0].hWrite);
	}

	input.hScreen = GetStdHandle(STD_OUTPUT_HANDLE);

	SetConsoleOutputCP(CP_UTF8);

	if (0x22 == *cmdLine)
	{
		cmdLine++;

		while (*cmdLine && (0x22 != *cmdLine))
		{
			cmdLine++;
		}

		if (0x22 == *cmdLine)
		{
			cmdLine++;
		}
	}
	else
	{
		while (*cmdLine && (0x20 < *cmdLine))
		{
			cmdLine++;
		}
	}

	while (*cmdLine && (0x20 >= *cmdLine))
	{
		cmdLine++;
	}

	if (!*cmdLine)
	{
		DWORD dw = GetEnvironmentVariableW(L"COMSPEC", comspec, (sizeof(comspec) / sizeof(comspec[0])) - 3);

		if (dw)
		{
			cmdLine = comspec;
		}
		else
		{
			cmdLine = NULL;
			exitCode = GetLastError();
		}
	}

	if (cmdLine && cmdLine[0])
	{
		nChannels = output.nChannels;

		while (nChannels--)
		{
			if (channel->bConsole)
			{
				if (SetConsoleMode(channel->hWrite, channel->mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING) &&
					GetConsoleScreenBufferInfo(channel->hWrite, &info))
				{
					bHaveConsole = TRUE;
				}
				else
				{
					exitCode = GetLastError();
				}
			}

			channel++;
		}
	}

	if (bHaveConsole)
	{
		HANDLE inputReadSide = INVALID_HANDLE_VALUE, outputWriteSide = INVALID_HANDLE_VALUE;
		HANDLE outputReadSide = INVALID_HANDLE_VALUE, inputWriteSide = INVALID_HANDLE_VALUE;

		if (CreatePipe(&inputReadSide, &inputWriteSide, NULL, 0) && CreatePipe(&outputReadSide, &outputWriteSide, NULL, 0))
		{
			HRESULT hr = CreatePseudoConsole(info.dwSize, inputReadSide, outputWriteSide, PSEUDOCONSOLE_INHERIT_CURSOR, &input.hPC);

			if (SUCCEEDED(hr))
			{
				HANDLE heap = GetProcessHeap();
				STARTUPINFOEX si;
				ZeroMemory(&si, sizeof(si));
				si.StartupInfo.cb = sizeof(si);
				size_t bytesRequired = 0;

				InitializeProcThreadAttributeList(NULL, 1, 0, &bytesRequired);

				si.lpAttributeList = HeapAlloc(heap, 0, bytesRequired);

				if (si.lpAttributeList)
				{
					if (InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytesRequired) &&
						UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, input.hPC, sizeof(input.hPC), NULL, NULL))
					{
						const size_t charsRequired = wcslen(cmdLine) + 1;
						PWSTR cmdLineMutable = HeapAlloc(heap, 0, sizeof(wchar_t) * charsRequired);

						if (cmdLineMutable)
						{
							BOOL b;
							PROCESS_INFORMATION pi;

							wcscpy_s(cmdLineMutable, charsRequired, cmdLine);

							b = CreateProcessW(NULL,
								cmdLineMutable,
								NULL,
								NULL,
								FALSE,
								EXTENDED_STARTUPINFO_PRESENT,
								NULL,
								NULL,
								&si.StartupInfo,
								&pi);

							if (!b)
							{
								exitCode = GetLastError();
							}

							CloseHandle(inputReadSide);
							CloseHandle(outputWriteSide);

							HeapFree(heap, 0, cmdLineMutable);
							cmdLineMutable = NULL;

							if (b)
							{
								DWORD ex;

								input.running = TRUE;
								input.hWrite = inputWriteSide;
								output.hRead = outputReadSide;

								threadInput = CreateThread(NULL, 0, input_thread, &input, 0, &tidInput);

								if (threadInput)
								{
									threadOutput = CreateThread(NULL, 0, output_thread, &output, 0, &tidOutput);

									if (threadOutput)
									{
										WaitForSingleObject(pi.hProcess, INFINITE);

										if (GetExitCodeProcess(pi.hProcess, &ex))
										{
											bWriteError = FALSE;
											exitCode = ex;
										}
										else
										{
											exitCode = GetLastError();
										}

										input.running = FALSE;
										SetEvent(input.hEvent);

										WaitForSingleObject(threadInput, INFINITE);
										CloseHandle(threadInput);
									}
									else
									{
										exitCode = GetLastError();
									}
								}
								else
								{
									exitCode = GetLastError();
								}

								CloseHandle(pi.hProcess);
								CloseHandle(pi.hThread);
							}
						}
						else
						{
							exitCode = ERROR_OUTOFMEMORY;
						}
					}
					else
					{
						exitCode = GetLastError();
					}

					HeapFree(heap, 0, si.lpAttributeList);
				}
				else
				{
					exitCode = ERROR_OUTOFMEMORY;
				}

				ClosePseudoConsole(input.hPC);

				if (threadOutput)
				{
					WaitForSingleObject(threadOutput, INFINITE);
					CloseHandle(threadOutput);
				}
			}
			else
			{
				exitCode = hr;
			}
		}
		else
		{
			exitCode = GetLastError();
		}
	}

	nChannels = output.nChannels;
	channel = output.channels;

	while (nChannels--)
	{
		if (channel->bConsole)
		{
			SetConsoleMode(channel->hWrite, channel->mode);
		}

		channel++;
	}

	SetConsoleMode(input.hRead, input.mode);

	if (bWriteError && exitCode)
	{
		char buf[256];
		DWORD dw = FormatMessageA(
			FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
			NULL,
			exitCode,
			0,
			buf,
			sizeof(buf) - 2,
			NULL);

		if (dw)
		{
			WriteFile(output.channels[1].hWrite, buf, dw, &dw, NULL);
		}
	}

	return exitCode;
}
