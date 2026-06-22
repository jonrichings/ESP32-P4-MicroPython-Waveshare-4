# 🚀 MicroPython & `mpremote` Cheat Sheet

This guide covers the most frequent commands used for developing on the Elecrow ESP32-P4.

## 1. Connection & Discovery
| Command | When to use it | Why? |
| :--- | :--- | :--- |
| `mpremote devs` | **First step** of any session. | Confirms which COM port the board is on (usually `COM4`). |
| `mpremote connect COM4 ...` | **Always.** | Tells `mpremote` which specific board to talk to. All other commands follow this. |

## 2. Running & Testing Code
| Command | How / When | why? |
| :--- | :--- | :--- |
| `mpremote connect COM4 run local_file.py` | **Standard Development.** | Uploads the file to RAM and runs it immediately. **Does not save to flash.** Perfect for rapid testing. |
| `mpremote connect COM4 exec "import gc; gc.collect()"` | **One-off commands.** | Runs a single line of Python code without needing a file. Useful for checking memory or resetting a pin. |
| `mpremote connect COM4 soft-reset` | **If things get weird.** | Reboots the MicroPython VM without a physical power cycle. Clears memory and restarts `main.py`. |

## 3. File Management (Flash Memory)
| Command | How / When | Why? |
| :--- | :--- | :--- |
| `mpremote connect COM4 cp local.py :remote.py` | **Deployment.** | Copies a file from your PC to the board's permanent flash. (Note the `:` prefix). |
| `mpremote connect COM4 ls` | **Inventory.** | Lists all files currently stored on the board's flash memory. |
| `mpremote connect COM4 cat :boot.py` | **Verification.** | Prints the contents of a file stored on the board to your terminal. |
| `mpremote connect COM4 rm :old_file.py` | **Cleanup.** | Deletes a file from the board's flash. |

## 4. Interactive REPL (The Shell)
| Command / Key | Action | Why? |
| :--- | :--- | :--- |
| `mpremote connect COM4 repl` | **Enter the Shell.** | Gives you the `>>>` prompt. You can type Python live and see immediate results. |
| **Ctrl + C** | **Interrupt.** | Stops a running script and returns you to the `>>>` prompt. |
| **Ctrl + D** | **Soft-Reset.** | Reboots the board while you are inside the REPL. |
| **Ctrl + E** | **Paste Mode.** | Allows you to paste a large block of code into the REPL at once. |
| **Ctrl + ]** | **Exit REPL.** | Returns you to your normal PowerShell/Command prompt. |

## 5. Troubleshooting (Windows "Port Busy")
| Command | How / When | Why? |
| :--- | :--- | :--- |
| `Stop-Process -Name "mpremote", "python" -Force` | **"Access Denied" error.** | Kills "ghost" connections that are locking the COM port. Only needed on Windows. |

---
> [!TIP]
> **Pro Tip**: You can chain commands! 
> `mpremote connect COM4 soft-reset connect COM4 run my_script.py`
> This ensures a fresh memory state before starting your new test.
