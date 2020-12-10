set -e

cp ./mamehub64.exe ./mamehub64.pdb ../mame_deploy
pushd ../mame_deploy
7za a mamehub_windows_beta.7z *
mv mamehub_windows_beta.7z ../
popd
rsync -raz --progress ../mamehub_windows_beta.7z pawn@puzzleracer.com:/var/www/html/MAMEHub/
