# ObjectManager

ObjectPulse、ObjectFrame、ObjectArc の設定ファイルを編集するウィンドウです。各アプリの設定画面はなく、設定の実体はこのファイルのままです。

| アプリ | ファイル |
| --- | --- |
| ObjectPulse | `%USERPROFILE%\.config\objectpulse\pulse.conf` |
| ObjectFrame | `%USERPROFILE%\.config\ofwm\ofwm.conf` |
| ObjectArc | `%USERPROFILE%\.config\objectarc\arc.conf` |

「保存して反映」は、画面にある項目だけを書き換えます。行末のコメント、キーバインド、ウィンドウルール、`exec-once`、`folder`、`search_url` など、画面にない行はそのまま残します。ファイルがまだ無いときは、画面の項目だけを書いたファイルを作ります。書いていない項目は、各アプリの既定値です。

保存したとき、起動しているアプリには次のように再読み込みを送ります。

| アプリ | 送り方 |
| --- | --- |
| ObjectPulse | `ObjectPulse reload` と同じ (`WM_APP+2`) |
| ObjectFrame | `ofwm dispatch reload` と同じ |
| ObjectArc | `ObjectArc reload` と同じ (`WM_APP+5`) |

起動していないアプリは、次に起動したときに保存した内容を読みます。

ObjectFrame のキーバインドとウィンドウルール、ObjectArc の `folder` と `search_url`、ObjectPulse のピン留め (`pins.txt`) は、それぞれのファイルを直接編集します。「設定ファイルを開く」は、表示中のページのファイルをメモ帳で開きます。

実行中のウィンドウは `ObjectManager.exe quit` で閉じます。

### インストーラー

バージョンは `0.1.0` です。GitHub Actions ([.github/workflows/build.yml](../.github/workflows/build.yml)) が push のたびに exe をビルドし、
[Inno Setup](https://jrsoftware.org/isinfo.php) でインストーラー (`ObjectManager-0.1.0-setup.exe`) とポータブル版 zip を作ります。
`VERSION` と同じ番号のタグ (`v0.1.0`) を push すると、GitHub の Releases に公開されます。

ローカルでインストーラーを作る場合は Inno Setup 6 を入れて次を実行します。

```powershell
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0 installer\ObjectManager.iss
```
