$filePath = "apps/luna-ps5/src/display/luna_clay_ui.cpp"
$content = Get-Content $filePath -Raw

# Add padding after sizing if childGap appears but padding doesn't exist in the layout block
# Pattern: .layout = { ... .sizing = { ... } (no .padding) .childGap = ...
$newContent = [regex]::Replace($content, 
    '(\.layout\s*=\s*\{[^}]*)\n\s*(\.\s*sizing\s*=)\s*\{([^}]*)\}\s*,?\n(\s*)(\.\s*childGap)',
    '$1`n$2`n                    .padding = { 0, 0, 0, 0 },`n$3`n$4$5'
)

Set-Content $filePath -Value $newContent -NoNewline

Write-Host "Fixed clay layout files"
