/************************************
 * Copyright (c) 2025 Roger Brown.
 * Licensed under the MIT License.
 */

#include <windows.h>
#include <winerror.h>
#include <stdio.h>
#include <stdlib.h>

struct conlog_output_channel
{
	DWORD mode;
	int cp;
	BOOL bConsole;
	HANDLE hWrite;
};

struct conlog_output
{
	HANDLE hRead;
	int nChannels;
	struct conlog_output_channel channels[2];
};

struct conlog_input
{
	DWORD mode;
	HANDLE hRead, hWrite, hQuit;
	HPCON hPC;
};

static DWORD CALLBACK output_thread(LPVOID pv)
{
	struct conlog_output* state = pv;
	char buf[4096];
	DWORD offset = 0;
	DWORD dwRead;

	while (ReadFile(state->hRead, buf + offset, sizeof(buf) - offset, &dwRead, NULL))
	{
		int nChannels = state->nChannels;
		struct conlog_output_channel* channel = state->channels;

		if (dwRead == 0) break;

		while (nChannels--)
		{
			const BYTE* p = buf;
			DWORD len = dwRead + offset;

			while (len)
			{
				DWORD dwWrite;
				BOOL bWrite;

				if (channel->bConsole)
				{
					bWrite = WriteConsoleA(channel->hWrite, p, len, &dwWrite, NULL);
				}
				else
				{
					bWrite = WriteFile(channel->hWrite, p, len, &dwWrite, NULL);
				}

				if (!bWrite) break;
				if (!dwWrite) break;

				p += dwWrite;
				len -= dwWrite;
			}

			channel++;
		}
	}

	return 0;
}

static DWORD CALLBACK input_thread(LPVOID pv)
{
	struct conlog_input* state = pv;

	while (TRUE)
	{
		HANDLE hEvent[] = { state->hRead,state->hQuit };
		INPUT_RECORD input;
		DWORD dw;

		if (WAIT_OBJECT_0 != WaitForMultipleObjects(2, hEvent, FALSE, INFINITE))
		{
			break;
		}

		if (!ReadConsoleInput(state->hRead, &input, 1, &dw))
		{
			break;
		}

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

					read_len = WideCharToMultiByte(CP_UTF8, 0, &wide, 1, read_buffer, sizeof(read_buffer), NULL, NULL);
				}
				else
				{
					char code = 0;
					int arg = 0;

					switch (input.Event.KeyEvent.wVirtualKeyCode)
					{
					case VK_ESCAPE:
						code = 'P';
						break;
					case VK_END:
						code = '~'; arg = 4;
						break;
					case VK_PRIOR:
						code = '~'; arg = 5;
						break;
					case VK_NEXT:
						code = '~'; arg = 6;
						break;
					case VK_HOME:
						code = '~'; arg = 1;
						break;
					case VK_INSERT:
						code = '~'; arg = 2;
						break;
					case VK_DELETE:
						code = '~'; arg = 3;
						break;
					case VK_LEFT:
						code = 'D';
						break;
					case VK_RIGHT:
						code = 'C';
						break;
					case VK_UP:
						code = 'A';
						break;
					case VK_DOWN:
						code = 'B';
						break;
					}

					if (code)
					{
						read_buffer[read_len++] = 27;

						if (arg)
						{
							read_len += sprintf_s(read_buffer + read_len, sizeof(read_buffer) - read_len, "[%d%c", arg, code);
						}
						else
						{
							read_buffer[read_len++] = '[';
							read_buffer[read_len++] = code;
						}
					}
				}

				if (read_len)
				{
					DWORD dw;
					WriteFile(state->hWrite, read_buffer, read_len, &dw, NULL);
				}
			}

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
	BOOL bHaveConsole = FALSE;

	ZeroMemory(&info, sizeof(info));
	ZeroMemory(&output, sizeof(output));
	ZeroMemory(&input, sizeof(input));

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

	input.hQuit = CreateEvent(NULL, FALSE, FALSE, NULL);

	output.channels[0].cp = CP_UTF8;
	output.channels[0].hWrite = GetStdHandle(STD_OUTPUT_HANDLE);
	output.channels[0].bConsole = GetConsoleMode(output.channels[0].hWrite, &output.channels[0].mode);

	output.channels[1].cp = CP_UTF8;
	output.channels[1].hWrite = GetStdHandle(STD_ERROR_HANDLE);
	output.channels[1].bConsole = GetConsoleMode(output.channels[1].hWrite, &output.channels[1].mode);

	if (output.channels[0].bConsole && output.channels[1].bConsole)
	{
		fprintf(stderr, "Both stdout and stderr are console devices\n");
		fflush(stderr);

		return ERROR_NOT_SUPPORTED;
	}

	if ((!output.channels[0].bConsole) && !output.channels[1].bConsole)
	{
		fprintf(stderr, "No console output\n");
		fflush(stderr);

		return ERROR_NOT_SUPPORTED;
	}

	output.nChannels = 2;

	if (output.channels[1].bConsole)
	{
		SetStdHandle(STD_OUTPUT_HANDLE, output.channels[1].hWrite);
		SetStdHandle(STD_ERROR_HANDLE, output.channels[0].hWrite);
	}

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
			HRESULT hr = CreatePseudoConsole(info.dwSize, inputReadSide, outputWriteSide, 0, &input.hPC);

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
											exitCode = ex;
										}

										SetEvent(input.hQuit);

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

	return exitCode;
}
