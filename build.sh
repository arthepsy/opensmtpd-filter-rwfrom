#!/bin/sh

_cdir=$(cd -- "$(dirname "$0")" && pwd)
_ose="${_cdir}/OpenSMTPD-extras"

_ierr() {
	if [ $1 -ne 0 ]; then
		echo "err: $2"
		exit 1
	fi
}

cd -- "${_cdir}"
if [ ! -d "${_ose}" ]; then
	echo "- cloning OpenSMTPD-extras"
	git clone https://github.com/OpenSMTPD/OpenSMTPD-extras.git "${_ose}"
	_ierr $? "git clone failed."
fi

echo "- updating OpenSMTPD-extras"
cd -- "${_ose}"
git pull
_ierr $? "git pull failed."
cd -- "${_cdir}"

echo "- cleaning OpenSMTPD-extras"
cd -- "${_ose}"
git reset --hard origin/master
_ierr $? "git reset failed."
git clean -fdx
_ierr $? "git clean failed."
cd -- "${_cdir}"

echo "- patching OpenSMTPD-extras"
patch -d "${_ose}" < ./patch-OpenSMTPD-extras
_ierr $? "patch failed."

echo "- copying filter-rwfrom"
cp -R ./filter-rwfrom "${_ose}/extras/wip/filters/"
_ierr $? "copy failed."

echo "- building OpenSMTPD-extras"
cd -- "${_ose}"
./bootstrap
_ierr $? "bootstrap failed."

_conf=""
case `uname -s` in
	Linux) _conf="--with-privsep-user=smtpd" ;;
esac

./configure "${_conf}"
_ierr $? "configure failed."
make
_ierr $? "make failed."

echo "- building filter-rwfrom"
cd -- "./extras/wip/filters/filter-rwfrom"
make
_ierr $? "make failed."

echo "- success"
cd -- "${_cdir}"
ls -al -- "${_ose}/extras/wip/filters/filter-rwfrom/filter-rwfrom"


