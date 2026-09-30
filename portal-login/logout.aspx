<%@ Page Language="C#" %>
<%@ Import Namespace="System.Web.Security" %>
<script runat="server">
    protected void Page_Load(object sender, EventArgs e)
    {
        FormsAuthentication.SignOut();
        Response.Redirect("login.aspx");
    }
</script>
