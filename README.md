# BatchGram

BatchGram is an unofficial fork of [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) (itself based on [Telegram Desktop](https://github.com/telegramdesktop/tdesktop)) that adds batch downloads from search.

BatchGram is not affiliated with or endorsed by AyuGram or Telegram. It is licensed under the [GPLv3](LICENSE), like upstream.

## Batch downloads

Download all videos that match a search in a channel.

1. In a channel, open the search (magnifier icon) and search for a phrase or hashtag.
2. Right-click any result in the left panel and choose **Select**. Tick the posts you want, or press the **Select all** icon to select every match in the channel, including ones that are not loaded yet.

3. Press the **Download** icon. A box opens:
   - Optionally tick **Include photos**. Photos are saved next to their video, with the same name.
   - Check the folder. To pick another one, press **Change**.
   - Press **Download**.

4. The videos are added to Telegram's Downloads manager as they are found. The bar shows "Added N videos to Downloads" with a progress line. A message confirms when all are added. Press × to stop adding. Downloads that are already added keep going.

Notes:

- Downloads use Telegram's normal download path.
- Progress is not kept if the app closes before all posts are added.
- Photos are saved to the folder but are not listed in Downloads.

## Downloads

Download BatchGram from this repository's [Releases page](../../releases).

The Windows build is unsigned, so Windows SmartScreen may warn on first run.

## About AyuGram

![AyuGram Logo](.github/AyuGram.png) ![AyuChan](.github/AyuChan.png)

[ English  |   [Русский](README-RU.md) ]

### Features

- Full ghost mode (flexible)
- Messages history
- Anti-recall
- Font customization
- Streamer mode
- Local Telegram Premium
- Translator
- Media preview & quick reaction on force click (macOS)
- Enhanced appearance

And many more. Check out our [Documentation](https://docs.ayugram.one/desktop/).

<h3>
  <details>
    <summary>Preview</summary>
    <table>
      <tr>
        <td><img src='.github/demos/demo1.png' width='268' alt='Preferences'></td>
        <td><img src='.github/demos/demo2.png' width='268' alt='AyuGram Options'></td>
        <td><img src='.github/demos/demo3.png' width='268' alt='Message Filters'></td>
      </tr>
      <tr>
        <td><img src='.github/demos/demo4.png' width='268' alt='Appearance'></td>
        <td><img src='.github/demos/demo5.png' width='268' alt='Chats'></td>
      </tr>
    </table>
  </details>
</h3>

### Official AyuGram downloads

#### Windows

##### Official

You can download prebuilt Windows binary from [Releases tab](https://github.com/AyuGram/AyuGramDesktop/releases) or from
the [Telegram channel](https://t.me/AyuGramReleases).

##### Winget

```bash
winget install RadolynLabs.AyuGramDesktop
```

##### Scoop

```bash
scoop bucket add extras
scoop install ayugram
```

##### Self-built

Follow [official guide](https://github.com/AyuGram/AyuGramDesktop/blob/dev/docs/building-win-x64.md) if you want to
build by yourself.

#### macOS

##### Official

You can download prebuilt macOS package from [Releases tab](https://github.com/AyuGram/AyuGramDesktop/releases).

##### Homebrew

```bash
brew install --cask ayugram
```

#### Arch Linux

##### From source (recommended)

Install `ayugram-desktop` from [AUR](https://aur.archlinux.org/packages/ayugram-desktop).

##### Prebuilt binaries

Install `ayugram-desktop-bin` from [AUR](https://aur.archlinux.org/packages/ayugram-desktop-bin).

Note: these binaries aren't officially maintained by us.

#### NixOS

##### Flake (recommended)

Install `ayugram-desktop` from [ndfined-crp/ayugram-desktop](https://github.com/ndfined-crp/ayugram-desktop)

##### Nixpkgs

Install `ayugram-desktop` from [nixpkgs](https://search.nixos.org/packages?channel=unstable&show=ayugram-desktop)

#### ALT Linux

[Sisyphus](https://packages.altlinux.org/en/sisyphus/srpms/ayugram-desktop/)

#### Gentoo Linux

See [this repository](https://codeberg.org/OverLessArtem/ayugram-ebuild-gentoo) for installation manual.

#### Void Linux
See [this repository](https://codeberg.org/OverLessArtem/ayugram-template-void) for installation manual.

#### EPM

`epm play ayugram`

#### Fedora

From [RPM Fusion](https://admin.rpmfusion.org/pkgdb/package/free/ayugram-desktop/) repository.

```bash
dnf install ayugram-desktop
```

#### Any other Linux distro

Flatpak: https://github.com/0FL01/AyuGramDesktop-flatpak

Or follow the [official guide](https://github.com/AyuGram/AyuGramDesktop/blob/dev/docs/building-linux.md).

### Donation

Enjoy using **AyuGram**? Consider sending us a tip!

[Here's available methods.](https://docs.ayugram.one/donate/)

### Credits

#### Telegram clients

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop)
- [Kotatogram](https://github.com/kotatogram/kotatogram-desktop)
- [64Gram](https://github.com/TDesktop-x64/tdesktop)
- [Forkgram](https://github.com/forkgram/tdesktop)

#### Libraries used

- [JSON for Modern C++](https://github.com/nlohmann/json)
- [SQLite](https://github.com/sqlite/sqlite)
- [sqlite_orm](https://github.com/fnc12/sqlite_orm)
- [androidx sources](https://github.com/androidx/androidx)

#### Icons

- [Solar Icon Set](https://www.figma.com/community/file/1166831539721848736)

#### Bots

- [TelegramDB](https://t.me/tgdatabase) for username lookup by ID (until closing free inline mode at 2 April 2026)
