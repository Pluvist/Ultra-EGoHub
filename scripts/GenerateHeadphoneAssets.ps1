[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$AssetRoot,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [switch]$ManifestOnly
)

$ErrorActionPreference='Stop'
$resolvedAssetRoot=Resolve-Path -LiteralPath $AssetRoot
if($resolvedAssetRoot.Provider.Name -ne 'FileSystem' -or
   -not (Test-Path -LiteralPath $resolvedAssetRoot.ProviderPath -PathType Container)) {
    throw 'AssetRoot must be an existing filesystem directory.'
}
$assetDirectory=[IO.Path]::GetFullPath($resolvedAssetRoot.ProviderPath)
$outputFile=[IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath))
$namespace='http://wixtoolset.org/schemas/v4/wxs'

function Get-StableId([string]$prefix,[string]$relativePath) {
    # Match Windows installer path identity while remaining independent of the
    # checkout location, discovery order, path separators and file timestamps.
    $normalized=$relativePath.Replace([char]92,[char]47).ToLowerInvariant()
    $bytes=[Text.Encoding]::UTF8.GetBytes($normalized)
    $digest=[Security.Cryptography.SHA256]::HashData($bytes)
    $hex=[Convert]::ToHexString($digest).Substring(0,32)
    return $prefix+$hex
}

$files=[Collections.Generic.SortedDictionary[string,IO.FileInfo]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($file in Get-ChildItem -LiteralPath $assetDirectory -Recurse -File) {
    if($file.Extension -notin '.png','.json') { continue }
    if($ManifestOnly -and $file.Extension -ne '.json') { continue }
    $relative=[IO.Path]::GetRelativePath($assetDirectory,$file.FullName).Replace([char]92,[char]47)
    if([IO.Path]::IsPathRooted($relative) -or $relative -eq '..' -or $relative.StartsWith('../')) {
        throw "Asset escapes AssetRoot: $relative"
    }
    if($files.ContainsKey($relative)) { throw "Duplicate Windows asset path: $relative" }
    $files.Add($relative,$file)
}
if($files.Count -eq 0) { throw 'AssetRoot contains no PNG images or JSON manifests.' }
if(-not $ManifestOnly -and -not @($files.Values | Where-Object Extension -eq '.png').Count) { throw 'AssetRoot contains no PNG images.' }
if(-not @($files.Values | Where-Object Extension -eq '.json').Count) { throw 'AssetRoot contains no JSON manifest.' }

$xml=[Xml.XmlDocument]::new()
[void]$xml.AppendChild($xml.CreateXmlDeclaration('1.0','utf-8',$null))
$wix=$xml.CreateElement('Wix',$namespace)
[void]$xml.AppendChild($wix)
$fragment=$xml.CreateElement('Fragment',$namespace)
[void]$wix.AppendChild($fragment)
$directoryRef=$xml.CreateElement('DirectoryRef',$namespace)
$directoryRef.SetAttribute('Id','INSTALLFOLDER')
[void]$fragment.AppendChild($directoryRef)

$directories=[Collections.Generic.Dictionary[string,Xml.XmlElement]]::new([StringComparer]::OrdinalIgnoreCase)
$assets=$xml.CreateElement('Directory',$namespace)
$assets.SetAttribute('Id',(Get-StableId 'HPDir_' 'Assets'))
$assets.SetAttribute('Name','Assets')
[void]$directoryRef.AppendChild($assets)
$headphones=$xml.CreateElement('Directory',$namespace)
$headphones.SetAttribute('Id',(Get-StableId 'HPDir_' 'Assets/Headphones'))
$headphones.SetAttribute('Name','Headphones')
[void]$assets.AppendChild($headphones)
$directories.Add('Assets',$assets)
$directories.Add('Assets/Headphones',$headphones)

$components=$xml.CreateElement('ComponentGroup',$namespace)
$components.SetAttribute('Id','HeadphoneImageAssets')
[void]$fragment.AppendChild($components)

foreach($entry in $files.GetEnumerator()) {
    $installPath='Assets/Headphones/'+$entry.Key
    $relativeParent=[IO.Path]::GetDirectoryName($entry.Key).Replace([char]92,[char]47)
    $directoryPath='Assets/Headphones'
    $directory=$headphones
    if($relativeParent) {
        foreach($part in $relativeParent.Split('/')) {
            $directoryPath+='/'+$part
            if(-not $directories.ContainsKey($directoryPath)) {
                $child=$xml.CreateElement('Directory',$namespace)
                $child.SetAttribute('Id',(Get-StableId 'HPDir_' $directoryPath))
                $child.SetAttribute('Name',$part)
                [void]$directory.AppendChild($child)
                $directories.Add($directoryPath,$child)
            }
            $directory=$directories[$directoryPath]
        }
    }
    $component=$xml.CreateElement('Component',$namespace)
    $component.SetAttribute('Id',(Get-StableId 'HPCmp_' $installPath))
    $component.SetAttribute('Directory',$directory.GetAttribute('Id'))
    $component.SetAttribute('Guid','*')
    $fileElement=$xml.CreateElement('File',$namespace)
    $fileElement.SetAttribute('Id',(Get-StableId 'HPFile_' $installPath))
    $fileElement.SetAttribute('Name',$entry.Value.Name)
    $fileElement.SetAttribute('Source',$entry.Value.FullName)
    $fileElement.SetAttribute('KeyPath','yes')
    [void]$component.AppendChild($fileElement)
    [void]$components.AppendChild($component)
}

$parent=[IO.Path]::GetDirectoryName($outputFile)
if($parent) { [void][IO.Directory]::CreateDirectory($parent) }
$settings=[Xml.XmlWriterSettings]::new()
$settings.Encoding=[Text.UTF8Encoding]::new($false)
$settings.Indent=$true
$settings.NewLineChars="`r`n"
$writer=[Xml.XmlWriter]::Create($outputFile,$settings)
try { $xml.Save($writer) } finally { $writer.Dispose() }
Write-Output "Generated HeadphoneImageAssets with $($files.Count) files: $outputFile"
