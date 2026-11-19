using System;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

static class Program
{
    [DllImport("user32.dll")]
    static extern bool SetProcessDPIAware();

    [STAThread]
    static void Main(string[] args)
    {
        try { SetProcessDPIAware(); } catch { }

        // argv: request.txt response.txt debug(0|1) log.txt
        if (args.Length < 2) return;
        string reqPath = args[0];
        string resPath = args[1];
        bool debug = args.Length > 2 && args[2] == "1";
        string logPath = args.Length > 3 ? args[3] : "";

        int maxChars = 42;
        string desc = "";
        string existing = "";
        try
        {
            string[] lines = File.ReadAllLines(reqPath, Encoding.UTF8);
            if (lines.Length > 0)
            {
                int parsed;
                if (int.TryParse(lines[0].Trim(), out parsed) && parsed > 0) maxChars = parsed;
            }
            if (lines.Length > 1) desc = lines[1];
            if (lines.Length > 2) existing = lines[2];
        }
        catch { }

        PanelForm.ConfigureLogging(logPath, debug);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        var form = new PanelForm(desc, existing, maxChars, resPath);
        Application.Run(form);
        PanelForm.AfterClose(form.GameHandle);
    }
}

class PanelForm : Form
{
    const uint WM_ACTIVATEAPP = 0x001C;
    static readonly object logLock = new object();
    static string logPath = "";
    static bool debug;

    public static void ConfigureLogging(string path, bool enabled)
    {
        logPath = path ?? "";
        debug = enabled;
    }

    static void PLog(string line)
    {
        if (!debug || logPath.Length == 0) return;
        try
        {
            lock (logLock)
            {
                File.AppendAllText(logPath, DateTime.Now.ToString("HH:mm:ss.fff") + "  " + line + Environment.NewLine,
                                   new UTF8Encoding(false));
            }
        }
        catch { }
    }

    readonly string resPath;
    readonly TextBox box;
    IntPtr game;
    bool completed;

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr FindWindow(string className, string windowName);
    [DllImport("user32.dll")]
    static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")]
    static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("kernel32.dll")]
    static extern uint GetCurrentThreadId();
    [DllImport("user32.dll", SetLastError = true)]
    static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool attach);
    [DllImport("user32.dll")]
    static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    static extern IntPtr SetFocus(IntPtr hWnd);
    [DllImport("user32.dll")]
    static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")]
    static extern bool IsIconic(IntPtr hWnd);

    [StructLayout(LayoutKind.Sequential)]
    struct RECT { public int Left, Top, Right, Bottom; }

    public IntPtr GameHandle { get { return game; } }

    static IntPtr FindGame()
    {
        return FindWindow("GLFW30", null);
    }

    static void Restore(IntPtr game, string stage)
    {
        if (game == IntPtr.Zero) game = FindGame();
        if (game == IntPtr.Zero)
        {
            PLog(stage + ": game window not found");
            return;
        }
        if (IsIconic(game))
        {
            ShowWindow(game, 9);
            PLog(stage + ": game was minimized, restored");
        }
        uint pid;
        uint gameThread = GetWindowThreadProcessId(game, out pid);
        uint myThread = GetCurrentThreadId();
        bool attached = false;
        if (gameThread != 0 && gameThread != myThread)
        {
            attached = AttachThreadInput(myThread, gameThread, true);
            if (!attached) PLog(stage + ": AttachThreadInput failed, err=" + Marshal.GetLastWin32Error());
        }
        bool sfw = SetForegroundWindow(game);
        IntPtr prev = SetFocus(game);
        if (attached) AttachThreadInput(myThread, gameThread, false);
        PostMessage(game, WM_ACTIVATEAPP, (IntPtr)1, (IntPtr)gameThread);
        PLog(stage + ": attached=" + attached + " SetForegroundWindow=" + sfw +
             " SetFocus(prev)=" + prev + " fgNow=" + GetForegroundWindow() + " game=" + game);
    }

    public static void AfterClose(IntPtr game)
    {
        if (game == IntPtr.Zero) game = FindGame();
        PLog("after-close: begin fg=" + GetForegroundWindow() + " game=" + game);
        for (int i = 0; i < 15; i++)
        {
            if (GetForegroundWindow() == game) break;
            Restore(game, "post#" + i);
            System.Threading.Thread.Sleep(100);
        }
        PLog("after-close: end fg=" + GetForegroundWindow() + " game=" + game);
    }

    public PanelForm(string desc, string existing, int maxChars, string resPath)
    {
        this.resPath = resPath;
        game = FindGame();
        PLog("open: maxChars=" + maxChars + " desc=\"" + desc + "\" existing=\"" + existing + "\" game=" + game);

        FormBorderStyle = FormBorderStyle.None;
        TopMost = true;
        ShowInTaskbar = false;
        StartPosition = FormStartPosition.Manual;
        BackColor = Color.FromArgb(24, 24, 28);
        Size = new Size(900, 180);
        KeyPreview = true;

        var title = new Label
        {
            Text = string.IsNullOrEmpty(desc) ? "输入文本" : desc,
            Font = new Font("Microsoft YaHei UI", 15f),
            ForeColor = Color.FromArgb(225, 225, 230),
            AutoSize = false,
            Dock = DockStyle.Top,
            Height = 46,
            TextAlign = ContentAlignment.MiddleLeft,
            Padding = new Padding(24, 0, 24, 0)
        };

        box = new TextBox
        {
            Font = new Font("Microsoft YaHei UI", 24f),
            BackColor = Color.FromArgb(40, 40, 46),
            ForeColor = Color.White,
            BorderStyle = BorderStyle.FixedSingle,
            MaxLength = maxChars,
            Text = existing.Length > maxChars ? existing.Substring(0, maxChars) : existing
        };
        box.SetBounds(24, 56, 852, 62);
        box.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

        var hint = new Label
        {
            Text = string.Format("Enter 提交    Esc 取消    {0} 字上限", maxChars),
            Font = new Font("Microsoft YaHei UI", 10f),
            ForeColor = Color.FromArgb(140, 140, 150),
            AutoSize = false,
            TextAlign = ContentAlignment.MiddleRight
        };
        hint.SetBounds(24, 132, 852, 32);
        hint.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;

        Controls.Add(title);
        Controls.Add(box);
        Controls.Add(hint);

        box.KeyDown += (s, e) =>
        {
            if (e.KeyCode == Keys.Enter) { e.SuppressKeyPress = true; Submit(); }
            else if (e.KeyCode == Keys.Escape) { e.SuppressKeyPress = true; Cancel(); }
        };
        KeyDown += (s, e) =>
        {
            if (e.KeyCode == Keys.Enter) { e.SuppressKeyPress = true; Submit(); }
            else if (e.KeyCode == Keys.Escape) { e.SuppressKeyPress = true; Cancel(); }
        };

        PositionOverGame();
    }

    void PositionOverGame()
    {
        if (game != IntPtr.Zero)
        {
            RECT r;
            if (GetWindowRect(game, out r))
            {
                int x = r.Left + ((r.Right - r.Left) - Width) / 2;
                int y = r.Top + ((r.Bottom - r.Top) - Height) / 2;
                Location = new Point(x, y);
                return;
            }
        }
        var b = Screen.PrimaryScreen.Bounds;
        Location = new Point(b.Left + (b.Width - Width) / 2, b.Top + (b.Height - Height) / 2);
    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        TopMost = true;
        Activate();
        BringToFront();
        box.Focus();
        box.SelectionStart = box.TextLength;
        PLog("shown: fg=" + GetForegroundWindow());
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        if (!completed)
        {
            completed = true;
            WriteResult(false, "");
            PLog("closed-by-x: cancel written");
        }
        base.OnFormClosing(e);
    }

    void Submit()
    {
        if (completed) return;
        completed = true;
        WriteResult(true, box.Text);
        PLog("submit: bytes=" + Encoding.UTF8.GetByteCount(box.Text) + " text=\"" + box.Text + "\"");
        Restore(game, "submit-preclose");
        Close();
    }

    void Cancel()
    {
        if (completed) return;
        completed = true;
        WriteResult(false, "");
        PLog("cancel");
        Restore(game, "cancel-preclose");
        Close();
    }

    void WriteResult(bool submitted, string text)
    {
        try
        {
            string tmp = resPath + ".tmp";
            File.WriteAllText(tmp, (submitted ? "1" : "0") + "\n" + text, new UTF8Encoding(false));
            File.Delete(resPath);
            File.Move(tmp, resPath);
        }
        catch { }
    }
}