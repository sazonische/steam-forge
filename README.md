# SteamForge

**Steam Guard on your computer.** Codes, trade confirmations and sign-in approvals for all your Steam accounts in one window, for Windows and Linux. Free and open source.

[Русская версия](README.ru.md)

## What it does

- **Steam Guard codes** for every account in one list. Codes and account names stay hidden until you click the eye, and the button next to a code copies it.
- **Trade and Market confirmations.** New ones arrive as a notification. You see the items first and then accept or decline. Nothing is ever confirmed without you.
- **Sign-in approval.** When you sign in to Steam on a computer or in a browser, approve it here, or let SteamForge read the QR code shown on your screen.
- **Steam Guard setup** for an account that does not have it yet, and **moving Steam Guard** from the Steam app on your phone to the computer.
- **Import** from Steam Desktop Authenticator (`.maFile`) and WinAuth.
- **Export** of an account to a `.maFile`, or a QR code to add it to another authenticator app.
- Steam nicknames and avatars, a tray icon, English and Russian.

## Download

Open [Releases](https://github.com/sazonische/steam-forge/releases) and download one file:

| Your computer | File |
|---|---|
| Windows 10 (version 1809 or newer) or Windows 11, 64-bit | `SteamForge-<version>-windows-x64.exe` |
| Linux, 64-bit: Ubuntu 20.04, Debian 11, Linux Mint 20, Fedora or newer | `SteamForge-<version>-linux-x64` |

There is nothing to install. Put the file in any folder and start it.

**Windows** may say *"Windows protected your PC"*. Click **More info**, then **Run anyway**. The warning appears because the program is not signed with a paid certificate.

**Linux** needs permission to run the file: right-click it, open **Properties** and turn on the option that lets it run as a program.

## Getting started

Click **Add account** and pick what fits you:

- **The Steam app on your phone gives your codes.** Choose **Sign in to Steam**, then **Move Steam Guard to SteamForge**. Steam sends a text message to your phone number. From then on SteamForge gives the codes, and the phone app stops.
- **Your account has no Steam Guard codes yet.** Choose **Sign in to Steam**, then **Set up Steam Guard**. Steam sends a code by text message or email.
- **You already use Steam Desktop Authenticator or WinAuth.** Choose **Import from files** and select your `.maFile` files (with `manifest.json` if they are encrypted) or the WinAuth export.

Your Steam password is only used to sign in and is never saved.

## Write down your recovery code

When you set up or move Steam Guard, SteamForge shows a **recovery code** like `R12345`. Write it down on paper and keep it safe. It is the only way to remove Steam Guard from your account if you ever lose SteamForge and its files. You can see the code again in the account panel.

## Is it safe?

- Everything stays on your computer. SteamForge talks only to Steam. In the trade window, an item can be opened in a 3D viewer on reskins.gg, in your browser and only when you click it.
- Nothing happens on its own. Each confirmation and sign-in is approved by you, and every window shows which account it is for.
- The QR code for another app stays blurred until you click **Show QR code**.
- Removing an account from SteamForge does not turn off Steam Guard on Steam.
- The source code is open, and the release files are built automatically on GitHub from it.

### Encryption

Out of the box, your accounts are stored on the computer **without encryption**, and SteamForge tells you so. Click **Turn on encryption** at the top of the window to protect them:

- **Windows:** Windows Hello guards the data. SteamForge asks for your PIN, face or fingerprint at every start and before encryption is turned off.
- **Linux:** the key goes to a separate keyring called "SteamForge" with a password you choose. The system asks for this password at every start. In that window, untick **Automatically unlock…**, otherwise it is asked only once.
- Without Windows Hello or a keyring, encryption still works: the key is then tied to your user account, and nothing is asked at start.

Encryption protects copies of your data: backups, cloud folders, a lost or stolen disk. It does not protect against a virus on your computer while SteamForge is open.

Encrypted data opens only on this computer and under your user account. **Before you reinstall the system or move to another computer, export your accounts:** **Manage → Export to .maFile…**

Keep exported `.maFile` files and the QR code private. Whoever has them gets your Steam Guard codes.

## Questions

**The codes don't work.** SteamForge sets its clock by Steam's own time, and the line at the bottom of the window shows whether that worked. Check your internet connection.

**Can I keep using the same account on my phone?** If you imported a `.maFile`, every program with that file shows the same codes. Moving Steam Guard from the phone turns the phone app off.

**SteamForge does not start on Linux.** Some systems lack a small library it needs. Install it once: `sudo apt install libxcb-cursor0` on Ubuntu, Debian and Linux Mint, or `sudo dnf install xcb-util-cursor` on Fedora.

**Where are my files?** On Windows in `%APPDATA%\SteamForge`, on Linux in `~/.local/share/SteamForge`. To keep everything next to the program, for example on a USB stick, put an empty file named `.portable` beside it. The data then goes to the `SteamForgeData` folder.

**Can I sync with Google?** Not yet. Encrypted sync through Google Drive is planned.

## Coming later

- Encrypted sync through Google Drive.
- Removing Steam Guard with the recovery code and adding a phone number.
- Inventories and sending trade offers.

## License

[MIT](LICENSE). SteamForge is not affiliated with Valve or Steam.
