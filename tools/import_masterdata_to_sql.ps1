param(
    [Parameter(Mandatory=$true)][string]$JsonPath,
    [Parameter(Mandatory=$true)][string]$Server,
    [Parameter(Mandatory=$true)][string]$Database,
    [Parameter(Mandatory=$true)][string]$User,
    [Parameter(Mandatory=$true)][string]$Password
)

$ErrorActionPreference = 'Stop'

function Add-Param($cmd, [string]$name, $value) {
    if ($null -eq $value) {
        $p = $cmd.Parameters.AddWithValue("@$name", [DBNull]::Value)
    } else {
        $p = $cmd.Parameters.AddWithValue("@$name", $value)
    }
}

function Exec-NonQuery($conn, $tran, [string]$sql) {
    $cmd = $conn.CreateCommand()
    $cmd.Transaction = $tran
    $cmd.CommandText = $sql
    [void]$cmd.ExecuteNonQuery()
}

function Insert-Rows($conn, $tran, [string]$table, [string[]]$columns, $rows) {
    if ($rows.Count -eq 0) { return }
    $colSql = ($columns | ForEach-Object { "[$_]" }) -join ', '
    $paramSql = ($columns | ForEach-Object { "@$_" }) -join ', '
    $sql = "INSERT INTO dbo.[$table] ($colSql) VALUES ($paramSql)"
    foreach ($row in $rows) {
        $cmd = $conn.CreateCommand()
        $cmd.Transaction = $tran
        $cmd.CommandText = $sql
        foreach ($col in $columns) {
            Add-Param $cmd $col $row.$col
        }
        [void]$cmd.ExecuteNonQuery()
    }
}

$json = Get-Content -LiteralPath $JsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
$cs = "Server=$Server;Database=$Database;User ID=$User;Password=$Password;TrustServerCertificate=True;Encrypt=False;"
$conn = [System.Data.SqlClient.SqlConnection]::new($cs)
$conn.Open()
$tran = $conn.BeginTransaction()

try {
    $stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
    foreach ($table in @('VEVOK', 'Telep', 'ARUK', 'Ar', 'RENDSZAM')) {
        Exec-NonQuery $conn $tran "SELECT * INTO dbo.[_backup_${table}_$stamp] FROM dbo.[$table];"
    }

    foreach ($table in @('Ar', 'RENDSZAM', 'Telep', 'ARUK', 'VEVOK')) {
        Exec-NonQuery $conn $tran "DELETE FROM dbo.[$table];"
        Exec-NonQuery $conn $tran "DBCC CHECKIDENT ('dbo.[$table]', RESEED, 0);"
    }

    Insert-Rows $conn $tran 'VEVOK' @(
        'Vevokod', 'Vevonev', 'Adoszam', 'Country', 'VevoIrSzam', 'VevoVaros',
        'VevoUtca', 'StreetType', 'VevoHsz', 'LotNumber', 'BankszamlaSzam',
        'CegjegyzekSzam', 'CegjegyzesreJogosult', 'CegjegyzoJogosultsaga',
        'SAP', 'Hitel', 'Contact', 'Mobil', 'email', 'TimeFormat',
        'ModifyInDate', 'KshId', 'EKAERStatus', 'Szerzodes'
    ) $json.VEVOK

    Insert-Rows $conn $tran 'Telep' @(
        'VevoKod', 'TelepKod', 'TelepHely', 'Country', 'ZipCode', 'City', 'Street',
        'StreetType', 'StreetNumber', 'LotNumber', 'Email', 'Phone',
        'Contact', 'VATNumber'
    ) $json.Telep

    Insert-Rows $conn $tran 'ARUK' @(
        'Arukod', 'Arunev', 'Egysegar', 'MEgyseg', 'MerlegValtoSzam',
        'Modositva', 'VatExemptionCase', 'VatExemptionReason', 'VTSZ',
        'Afakulcs', 'adrNumber'
    ) $json.ARUK

    Insert-Rows $conn $tran 'Ar' @(
        'Szerzodes', 'TelepKod', 'Arukod', 'Egysegar'
    ) $json.Ar

    Insert-Rows $conn $tran 'RENDSZAM' @(
        'Rendszam', 'PotKocsi', 'Country', 'PCountry'
    ) $json.RENDSZAM

    $tran.Commit()
    Write-Host "Import OK. Backup suffix: $stamp"
} catch {
    $tran.Rollback()
    throw
} finally {
    $conn.Close()
}
