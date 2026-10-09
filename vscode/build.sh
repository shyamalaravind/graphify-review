#!/bin/sh
# Packs the extension into plan-viewer.vsix, which `code --install-extension` installs.
# A .vsix is a zip with a manifest; this writes one directly, so building needs no npm.
set -e
cd "$(dirname "$0")"
version=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' package.json | head -1)
out=$PWD/plan-viewer.vsix
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

mkdir "$tmp/extension"
cp package.json extension.js "$tmp/extension/"
cat > "$tmp/[Content_Types].xml" <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension=".json" ContentType="application/json"/>
  <Default Extension=".js" ContentType="application/javascript"/>
  <Default Extension=".vsixmanifest" ContentType="text/xml"/>
</Types>
EOF
cat > "$tmp/extension.vsixmanifest" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="plan-viewer" Version="$version" Publisher="shyamalaravind"/>
    <DisplayName>Plan Viewer</DisplayName>
    <Description xml:space="preserve">Review an agent's plan inside VS Code, with its names resolved through the language servers.</Description>
    <Categories>Other</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="^1.90.0"/>
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace"/>
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code"/>
  </Installation>
  <Dependencies/>
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true"/>
  </Assets>
</PackageManifest>
EOF
rm -f "$out"
(cd "$tmp" && zip -qr "$out" '[Content_Types].xml' extension.vsixmanifest extension)
echo "built $out ($version)"
