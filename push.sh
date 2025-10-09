#!/usr/bin/env bash

# Pushing Script

echo "#!/usr/bin/env bash

get_version() {
  echo \"$(git describe --abbrev=0)\"
}" >"./src/version"

git add .

read -p "Commit Message: " INPUT
git commit -m "$INPUT"

git push
