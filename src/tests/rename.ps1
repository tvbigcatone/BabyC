Get-ChildItem -File | ForEach-Object {
    if ($_ -match '^([0-9]{2})_(.+)$') {
        $num = $Matches[1]
        $rest = $Matches[2]
        $new = "0$($num)_$rest"

        if (Test-Path $new) {
            Write-Host "Esiste già $new → elimino $($_.Name)"
            Remove-Item $_.FullName
        } else {
            Write-Host "Rinomino $($_.Name) → $new"
            Rename-Item $_.FullName $new
        }
    }
}
