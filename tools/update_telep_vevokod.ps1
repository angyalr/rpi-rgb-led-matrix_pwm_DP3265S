param(
    [Parameter(Mandatory=$true)][string]$JsonPath,
    [Parameter(Mandatory=$true)][string]$Server,
    [Parameter(Mandatory=$true)][string]$Database,
    [Parameter(Mandatory=$true)][string]$User,
    [Parameter(Mandatory=$true)][string]$Password
)

$ErrorActionPreference = 'Stop'

$json = Get-Content -LiteralPath $JsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
$cs = "Server=$Server;Database=$Database;User ID=$User;Password=$Password;TrustServerCertificate=True;Encrypt=False;"
$conn = [System.Data.SqlClient.SqlConnection]::new($cs)
$conn.Open()
$tran = $conn.BeginTransaction()

try {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    $backup = "_backup_Telep_before_vevokod_$stamp"
    $cmd = $conn.CreateCommand()
    $cmd.Transaction = $tran
    $cmd.CommandText = "SELECT * INTO dbo.[$backup] FROM dbo.Telep;"
    [void]$cmd.ExecuteNonQuery()

    foreach ($row in $json.Telep) {
        $cmd = $conn.CreateCommand()
        $cmd.Transaction = $tran
        $cmd.CommandText = "UPDATE dbo.Telep SET VevoKod = @VevoKod WHERE TelepKod = @TelepKod;"
        [void]$cmd.Parameters.AddWithValue("@VevoKod", $row.VevoKod)
        [void]$cmd.Parameters.AddWithValue("@TelepKod", $row.TelepKod)
        [void]$cmd.ExecuteNonQuery()
    }

    $tran.Commit()
    Write-Host "Telep.VevoKod update OK. Backup table: $backup"
} catch {
    $tran.Rollback()
    throw
} finally {
    $conn.Close()
}
