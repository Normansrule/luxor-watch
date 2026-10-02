# Publish Luxor to GitHub from a fresh Ubuntu terminal

Each block is copy-paste. Run them in order. Works on Ubuntu 22.04 / 24.04 / 26.04, native or WSL2.

## 1 · Tools (once per machine)

```bash
sudo apt-get update && sudo apt-get install -y git zip unzip wget
(type -p wget >/dev/null || sudo apt-get install -y wget) \
 && sudo mkdir -p -m 755 /etc/apt/keyrings \
 && out=$(mktemp) && wget -nv -O"$out" https://cli.github.com/packages/githubcli-archive-keyring.gpg \
 && sudo tee /etc/apt/keyrings/githubcli-archive-keyring.gpg < "$out" > /dev/null \
 && sudo chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg \
 && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
    | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null \
 && sudo apt-get update && sudo apt-get install -y gh
gh --version
```

## 2 · Sign in (once per machine)

Sign in as the account that should own the repo. The `workflow` scope lets the push include `.github/workflows`.

```bash
gh auth login --hostname github.com --git-protocol https --web --scopes workflow
gh auth setup-git
git config --global user.name  "$(gh api user --jq '.name // .login')"
git config --global user.email "$(gh api user --jq '.id')+$(gh api user --jq '.login')@users.noreply.github.com"
git config --global init.defaultBranch main
```

The e-mail line uses your private GitHub `noreply` address, so your real address never appears in public commits.

## 3 · Unpack the package

This finds `Luxor_v9_IDF5_5_5.zip` in your Windows Downloads (WSL) or in `~/Downloads`.

```bash
ZIP=$(ls /mnt/c/Users/*/Downloads/Luxor_v9_IDF5_5_5.zip ~/Downloads/Luxor_v9_IDF5_5_5.zip 2>/dev/null | head -n1)
echo "Using: ${ZIP:?Luxor_v9_IDF5_5_5.zip not found - put it in Downloads}"
mkdir -p ~/projects && cd ~/projects
rm -rf Luxor_v9 && unzip -q "$ZIP"
mv Luxor_v9 luxor-watch && cd ~/projects/luxor-watch
```

## 4 · Create the repo and push

The `sed` line points the README badges and links at *your* account.

```bash
cd ~/projects/luxor-watch
OWNER=$(gh api user --jq .login)
grep -rl "Normansrule/luxor-watch" README.md docs | xargs -r sed -i "s#Normansrule/luxor-watch#$OWNER/luxor-watch#g"
git init -b main
git add -A
git commit -m "Luxor v9: open-source smartwatch - firmware, CAD, BOM, wiring, requirements"
gh repo create luxor-watch --public --source . --remote origin --push \
  --description "Open-source luxury smartwatch: ESP32-S3 round touchscreen, 3D-printed case, full BOM and pin-by-pin wiring"
gh repo edit --add-topic esp32-s3,smartwatch,open-hardware,esp-idf,lvgl,openscad,3d-printing,wearable
```

## 5 · Release: binaries and STLs as downloads

```bash
cd ~/projects/luxor-watch
(cd hardware && zip -qr ../luxor-v9-stl.zip stl)
gh release create v9 \
  prebuilt/bootloader.bin prebuilt/partition-table.bin prebuilt/luxor_v9.bin prebuilt/SHA256SUMS.txt \
  luxor-v9-stl.zip \
  --title "Luxor v9" --notes-file docs/RELEASE_NOTES_v9.md
rm luxor-v9-stl.zip
gh repo view --web
```

The last line opens the repo in your browser. The **Actions** tab shows the first CI run: host tests, UI audit, CAD checks and the firmware build, about 6 minutes.

## Later: publish a new version

Unpack the new zip over the repo (your `.git` folder is kept), then commit, push and release. Replace `10` with the new number.

```bash
V=10
ZIP=$(ls /mnt/c/Users/*/Downloads/Luxor_v${V}_IDF5_5_5.zip ~/Downloads/Luxor_v${V}_IDF5_5_5.zip 2>/dev/null | head -n1)
cd ~/projects && rm -rf "Luxor_v$V" && unzip -q "$ZIP"
rsync -a --delete --exclude .git "Luxor_v$V/" luxor-watch/ && rm -rf "Luxor_v$V"
cd ~/projects/luxor-watch
OWNER=$(gh api user --jq .login)
grep -rl "Normansrule/luxor-watch" README.md docs | xargs -r sed -i "s#Normansrule/luxor-watch#$OWNER/luxor-watch#g"
git add -A && git commit -m "Luxor v$V" && git push
(cd hardware && zip -qr ../luxor-v$V-stl.zip stl)
gh release create "v$V" prebuilt/*.bin prebuilt/SHA256SUMS.txt "luxor-v$V-stl.zip" \
  --title "Luxor v$V" --notes-file "docs/RELEASE_NOTES_v$V.md"
rm "luxor-v$V-stl.zip"
```

`rsync` comes with Ubuntu; if it is missing: `sudo apt-get install -y rsync`.
