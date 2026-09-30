<%@ Page Language="C#" %>
<%@ Import Namespace="System.Security.Cryptography" %>
<%@ Import Namespace="System.Text" %>
<%@ Import Namespace="System.Web.Security" %>
<%@ Import Namespace="System.Web.Caching" %>
<script runat="server">
    private const string StoredUser = "CAMBIAR_USUARIO";
    private const string StoredSalt = "CAMBIAR_SALT_BASE64";
    private const string StoredHash = "CAMBIAR_HASH_SHA256_HEX";

    private const int MaxAttempts = 5;
    private static readonly TimeSpan LockoutWindow = TimeSpan.FromMinutes(15);

    private string ComputeHash(string password, string salt)
    {
        using (var sha = SHA256.Create())
        {
            byte[] bytes = sha.ComputeHash(Encoding.UTF8.GetBytes(salt + password));
            var sb = new StringBuilder();
            foreach (byte b in bytes) sb.Append(b.ToString("x2"));
            return sb.ToString();
        }
    }

    private string ClientIp
    {
        get { return Request.ServerVariables["REMOTE_ADDR"] ?? "unknown"; }
    }

    private int GetFailCount()
    {
        object v = Cache["loginfail_" + ClientIp];
        return v == null ? 0 : (int)v;
    }

    private void RegisterFailure()
    {
        string key = "loginfail_" + ClientIp;
        int count = GetFailCount() + 1;
        Cache.Insert(key, count, null, DateTime.Now.Add(LockoutWindow), Cache.NoSlidingExpiration);
    }

    private void ClearFailures()
    {
        Cache.Remove("loginfail_" + ClientIp);
    }

    protected void btnLogin_Click(object sender, EventArgs e)
    {
        if (GetFailCount() >= MaxAttempts)
        {
            lblError.Text = "Demasiados intentos fallidos. Espera unos minutos e intenta de nuevo.";
            lblError.Visible = true;
            return;
        }

        string u = txtUser.Text.Trim();
        string p = txtPass.Text;
        if (u == StoredUser && ComputeHash(p, StoredSalt) == StoredHash)
        {
            ClearFailures();
            FormsAuthentication.RedirectFromLoginPage(u, chkRemember.Checked);
        }
        else
        {
            RegisterFailure();
            lblError.Text = "Usuario o contraseña incorrectos.";
            lblError.Visible = true;
        }
    }
</script>
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Centinela &middot; Acceso</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
<style>
  :root{
    --navy:#101828; --navy2:#1c2b3a; --blue:#00AEEF; --blue-dk:#0089c2;
    --ink:#101828; --sub:#667085; --line:#e4e7ec; --bg:#eef3f6;
    --danger:#b3261e; --danger-bg:#fdecea;
  }
  *{box-sizing:border-box; margin:0; padding:0}
  html,body{height:100%}
  body{
    font-family:'Inter',-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
    color:var(--ink); min-height:100vh; position:relative; overflow:hidden;
    display:flex; align-items:center; justify-content:center; padding:24px;
    background:var(--bg);
  }
  /* fondo geometrico diagonal, paleta institucional */
  body::before, body::after{
    content:""; position:fixed; inset:-10% -10%; z-index:0;
  }
  body::before{
    background:linear-gradient(135deg, transparent 0 40%, var(--navy) 40% 62%, transparent 62%);
    opacity:.95;
  }
  body::after{
    background:linear-gradient(135deg, transparent 0 52%, var(--blue) 52% 68%, transparent 68%);
    opacity:.85;
  }

  .card{
    position:relative; z-index:1; width:100%; max-width:360px;
    border-radius:16px; overflow:hidden; background:#fff;
    box-shadow:0 30px 70px rgba(16,24,40,.35);
  }
  .card-head{
    background:linear-gradient(135deg,var(--navy2),var(--navy));
    padding:34px 28px 56px; text-align:center; position:relative;
  }
  .card-head .brand-text{
    font-size:.78rem; letter-spacing:.16em; text-transform:uppercase; font-weight:700; color:#dbe1e8;
  }
  .avatar{
    position:absolute; left:50%; bottom:-32px; transform:translateX(-50%);
    width:72px; height:72px; border-radius:50%; background:#fff;
    border:4px solid #fff; box-shadow:0 8px 20px rgba(16,24,40,.25);
    display:flex; align-items:center; justify-content:center; overflow:hidden;
  }
  .avatar img{ width:100%; height:100%; object-fit:contain; padding:12px }

  .card-body{ padding:46px 30px 30px }
  .card-body h1{ font-size:1.05rem; font-weight:700; text-align:center; margin-bottom:26px; color:var(--ink) }

  .error{
    background:var(--danger-bg); border:1px solid #f3c8c4; color:var(--danger);
    font-size:.82rem; padding:9px 12px; border-radius:8px; margin-bottom:18px; text-align:center;
  }

  .field{
    display:flex; align-items:center; gap:10px;
    border-bottom:1.5px solid var(--line); padding:8px 2px 10px; margin-bottom:22px;
    transition:border-color .15s;
  }
  .field:focus-within{ border-color:var(--blue) }
  .field svg{ flex-shrink:0; color:#9aa4b2; width:18px; height:18px }
  .field:focus-within svg{ color:var(--blue) }
  .field input{
    flex:1; border:none; outline:none; font-family:'Inter',sans-serif;
    font-size:.92rem; color:var(--ink); background:transparent; padding:2px 0;
  }
  .field input::placeholder{ color:#a9b1bd }

  .row{ display:flex; align-items:center; justify-content:space-between; margin-bottom:24px }
  .remember{ display:flex; align-items:center; gap:8px; font-size:.8rem; color:var(--sub) }
  .switch{ position:relative; width:36px; height:20px; flex-shrink:0 }
  .switch input{ opacity:0; width:0; height:0; position:absolute }
  .switch .track{
    position:absolute; inset:0; background:#d7dce3; border-radius:999px; cursor:pointer; transition:.15s;
  }
  .switch .track::before{
    content:""; position:absolute; width:14px; height:14px; left:3px; top:3px;
    background:#fff; border-radius:50%; transition:.15s; box-shadow:0 1px 3px rgba(0,0,0,.3);
  }
  .switch input:checked + .track{ background:var(--blue) }
  .switch input:checked + .track::before{ transform:translateX(16px) }

  .btn{
    width:100%; padding:13px; border:none; border-radius:9px; cursor:pointer;
    font-family:'Inter',sans-serif; font-size:.92rem; font-weight:700; letter-spacing:.02em; color:#fff;
    background:linear-gradient(90deg,var(--blue),var(--blue-dk));
    box-shadow:0 8px 20px rgba(0,137,194,.35);
    transition:opacity .15s, transform .08s;
  }
  .btn:hover{ opacity:.93 }
  .btn:active{ transform:scale(.98) }

  .foot-note{
    display:flex; align-items:center; justify-content:center; gap:6px;
    margin-top:22px; font-size:.72rem; color:var(--sub);
  }
</style>
</head>
<body>
  <form id="form1" runat="server">
    <div class="card">
      <div class="card-head">
        <span class="brand-text">Una IPS</span>
        <div class="avatar">
          <img src="/logo.svg" alt="Una IPS" />
        </div>
      </div>
      <div class="card-body">
        <h1>Centinela &middot; Acceso</h1>

        <asp:Label ID="lblError" runat="server" CssClass="error" Visible="false" />

        <div class="field">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="8" r="4"/><path d="M4 21c0-4 4-6 8-6s8 2 8 6"/></svg>
          <asp:TextBox ID="txtUser" runat="server" placeholder="Usuario" autocomplete="username" />
        </div>
        <div class="field">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="11" width="18" height="10" rx="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>
          <asp:TextBox ID="txtPass" runat="server" TextMode="Password" placeholder="Contraseña" autocomplete="current-password" />
        </div>

        <div class="row">
          <label class="remember">
            <span class="switch">
              <asp:CheckBox ID="chkRemember" runat="server" />
              <span class="track"></span>
            </span>
            Recordarme
          </label>
        </div>

        <asp:Button ID="btnLogin" runat="server" Text="Ingresar" CssClass="btn" OnClick="btnLogin_Click" />

        <div class="foot-note">
          <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="11" width="18" height="10" rx="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>
          Conexión cifrada de extremo a extremo
        </div>
      </div>
    </div>
  </form>
</body>
</html>
