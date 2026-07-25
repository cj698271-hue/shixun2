[CmdletBinding()]
param(
    [string]$ProjectPath = (Join-Path $PSScriptRoot 'MDK-ARM\environment_monitor.uvprojx')
)

$requiredSources = @(
    'app_environment.c',
    'debug_log.c',
    'dht11.c',
    'esp12f.c',
    'mqtt_packet.c',
    'oled_display.c'
)

$oledSources = @(
    'font.c',
    'oled.c',
    'oled_extra_font.c',
    'oled_font.c'
)
$oledSourceRoot = '../Core/SYSTEM/OLED'
$oledIncludePath = '..\Core\SYSTEM\OLED'

$excludedSourcePaths = @(
    '../Core/Src/adc.c',
    '../Core/Src/mq2.c',
    '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc.c',
    '../Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc_ex.c'
)

if (-not (Test-Path -LiteralPath $ProjectPath)) {
    throw "Keil project was not found: $ProjectPath"
}

$document = New-Object System.Xml.XmlDocument
$document.PreserveWhitespace = $true
$document.Load($ProjectPath)

$target = $document.SelectSingleNode('/Project/Targets/Target')
$group = @($target.SelectNodes('./Groups/Group') |
    Where-Object { $_.SelectSingleNode('./GroupName').InnerText -eq 'Application/User/Core' })[0]

if ($null -eq $group) {
    throw 'Keil source group Application/User/Core was not found.'
}

$filesNode = $group.SelectSingleNode('./Files')
$added = @()
$removed = @()
$includePathNode = $target.SelectSingleNode('./TargetOption/TargetArmAds/Cads/VariousControls/IncludePath')

if ($null -ne $includePathNode -and $includePathNode.InnerText -notlike "*$oledIncludePath*") {
    $includePathNode.InnerText = if ([string]::IsNullOrWhiteSpace($includePathNode.InnerText)) {
        $oledIncludePath
    } else {
        $includePathNode.InnerText + ';' + $oledIncludePath
    }
}

foreach ($fileNode in @($document.SelectNodes('//File'))) {
    $filePathNode = $fileNode.SelectSingleNode('./FilePath')
    if ($null -ne $filePathNode -and $excludedSourcePaths -contains $filePathNode.InnerText) {
        $removed += $filePathNode.InnerText
        [void]$fileNode.ParentNode.RemoveChild($fileNode)
    }
}

foreach ($source in $requiredSources) {
    $relativePath = "../Core/Src/$source"
    $existing = $filesNode.SelectSingleNode("./File[FilePath='$relativePath']")
    if ($null -ne $existing) {
        continue
    }

    $fileNode = $document.CreateElement('File')
    $fileNameNode = $document.CreateElement('FileName')
    $fileNameNode.InnerText = $source
    [void]$fileNode.AppendChild($fileNameNode)
    $fileTypeNode = $document.CreateElement('FileType')
    $fileTypeNode.InnerText = '1'
    [void]$fileNode.AppendChild($fileTypeNode)
    $filePathNode = $document.CreateElement('FilePath')
    $filePathNode.InnerText = $relativePath
    [void]$fileNode.AppendChild($filePathNode)
    [void]$filesNode.AppendChild($fileNode)
    $added += $source
}

foreach ($source in $oledSources) {
    $relativePath = "$oledSourceRoot/$source"
    $existing = $filesNode.SelectSingleNode("./File[FilePath='$relativePath']")
    if ($null -ne $existing) {
        continue
    }

    $fileNode = $document.CreateElement('File')
    $fileNameNode = $document.CreateElement('FileName')
    $fileNameNode.InnerText = $source
    [void]$fileNode.AppendChild($fileNameNode)
    $fileTypeNode = $document.CreateElement('FileType')
    $fileTypeNode.InnerText = '1'
    [void]$fileNode.AppendChild($fileTypeNode)
    $filePathNode = $document.CreateElement('FilePath')
    $filePathNode.InnerText = $relativePath
    [void]$fileNode.AppendChild($filePathNode)
    [void]$filesNode.AppendChild($fileNode)
    $added += "OLED/$source"
}

$writerSettings = New-Object System.Xml.XmlWriterSettings
$writerSettings.Encoding = New-Object System.Text.UTF8Encoding($false)
$writerSettings.Indent = $true
$writerSettings.IndentChars = '  '
$writerSettings.NewLineChars = "`r`n"
$writerSettings.NewLineHandling = [System.Xml.NewLineHandling]::Replace
$writer = [System.Xml.XmlWriter]::Create($ProjectPath, $writerSettings)
$document.Save($writer)
$writer.Close()
if ($added.Count -eq 0) {
    Write-Output 'Keil project already contains all application sources.'
} else {
    Write-Output ("Restored Keil source references: " + ($added -join ', '))
}
if ($removed.Count -gt 0) {
    Write-Output ("Removed incompatible source references: " + ($removed -join ', '))
}
