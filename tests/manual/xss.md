# Hostile document

Opening this file must never run code or launch programs. Click every link:
the only acceptable results are "opens in browser", "opens in Notepad++",
or "nothing happens".

<script>document.body.style.background = 'red'</script>

<img src="x" onerror="document.body.style.background = 'red'">

<svg onload="document.body.style.background = 'red'"><circle r="10"/></svg>

<iframe src="https://example.com"></iframe>

<div style="position:fixed;inset:0;background:red">overlay must not appear</div>

- [javascript link](javascript:alert(1))
- [shell: link](shell:AppsFolder\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App)
- [file: executable](file:///C:/Windows/System32/calc.exe)
- [ms-settings link](ms-settings:privacy)
- [relative .cmd](evil.cmd)
- [relative .exe](calc.exe)
- [UNC exe](\\server\share\tool.exe)
- <a href="data:text/html,<script>alert(1)</script>">data: link</a>

> [!NOTE] <img src=x onerror=alert(1)>
> Alert title injection.

```mermaid
flowchart LR
    A["<img src=x onerror=alert(1)>"] --> B[click me]
    click B "javascript:alert(1)"
```
