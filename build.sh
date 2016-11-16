#!/bin/sh

_cdir=$(cd -- "$(dirname "$0")" && pwd)
_ose="${_cdir}/OpenSMTPD-extras"

_ierr() {
	if [ $1 -ne 0 ]; then
		echo "err: $2"
		exit 1
	fi
}

_usage() {
	echo "usage: $0 <filter_api_version>"
	echo ""
	echo "filter_api_version:"
	echo "   50        OpenSMTPD 5.9 and before"
	echo "   51        OpenSMTPD 6.0.0 - 6.0.2"
	echo "   51-head   OpenSMTPD 6.0.2 + patches"
	echo "   52        after OpenSMTPD 6.0.2"
	exit 1
}

_download() {
	local _filter_api="$1"
	case "${_filter_api}" in
		50)
			_fb="opensmtpd-extras-201606230001"
			;;
		51)
			_fb="opensmtpd-extras-201607122008"
			;;
		52)
			_fb="opensmtpd-extras-201610152004"
			;;
		51-head)
			cd -- "${_cdir}"
			if [ -d "${_ose}" ]; then
				grep -q 'arthepsy/OpenSMTPD-extras.git' OpenSMTPD-extras/.git/config
				if [ $? -ne 0 ]; then
					echo " - removing old OpenBSD-extras directory"
					rm -rf OpenSMTPD-extras
					_ierr $? "removing failed."
				fi
			fi
			if [ ! -d "${_ose}" ]; then
				echo "- cloning OpenSMTPD-extras"
				git clone https://github.com/arthepsy/OpenSMTPD-extras.git "${_ose}"
				_ierr $? "git clone failed."
				cd -- "${_ose}"
				git reset --hard origin/filter-api-51
				_ierr $? "git reset failed."
			else
				echo " - updating OpenBSD-extras"
				cd -- "${_ose}"
				git clean -fdx
				_ierr $? "git clean failed."
				git reset --hard origin/filter-api-51
				_ierr $? "git reset failed."
				git pull
				_ierr $? "git pull failed."
			fi
			;;
		*)
			_fb=""
			;;
	esac
	if [ X"${_fb}" != X"" ]; then
		cd -- "${_cdir}"
		_fn="${_fb}.tar.gz"
		if [ ! -f "$_fn" ]; then
			echo " - downloading ${_fb} ..."
			curl -sO "https://www.opensmtpd.org/archives/${_fn}"
			_ierr $? "download failed."
		fi
		echo " - extracting ${_fb} ..."
		tar -xzf "${_fn}"
		_ierr $? "extracting failed."
		if [ -d "${_ose}" ]; then
			echo " - removing old OpenBSD-extras directory"
			rm -rf OpenSMTPD-extras
			_ierr $? "removing failed."
		fi
		mv "${_fb}" OpenSMTPD-extras
		_ierr $? "renaming failed."
	fi
	return 0
}

_patch() {
	local _filter_api="$1"
	cd -- "${_ose}"
	echo " - patching configure"
	if [ ! -f "configure.ac" ]; then
		_ierr 1 "patching configure.ac failed (file not found)"
	fi
	_tmp=$(grep '/filter-void/Makefile' configure.ac)
	_pos1=0
	for _i in 5 4 3 2 1; do
		_tmpmx=$(echo "${_tmp}" | cut -d '/' -f $_i)
		if [ X"${_tmpmx}" = X"filter-void" ]; then
			_pos1=${_i}
			break
		fi
	done
	if [ ${_pos1} -eq 0 ]; then
		_ierr 1 "patching configure.ac failed (separator not found)"
	fi
	_pos2=$((${_pos1} - 1))
	_prefix=$(echo "${_tmp}" | cut -d '/' -f 1-$_pos2)
	_srcdir=$(echo $_prefix)
	_tmp="tmp.$$"
	awk "gsub(/filter-void\/Makefile/, \"filter-void\/Makefile\n$_prefix\/filter-rwfrom\/Makefile\")1" configure.ac > "${_tmp}" && mv "${_tmp}" configure.ac

	echo "- creating filter directory"
	_filterdir="${_srcdir}/filter-rwfrom"
	mkdir -p "${_filterdir}"
	_ierr $? "creating filter directory failed"

	echo "- copying filter source files"
	cp ../filter-rwfrom/* "${_filterdir}/"
	_ierr $? "copying filter source files failed"
	echo "- patching filter source"
	awk "gsub(/#define MY_FILTER_API_VERSION [0-9]*/, \"#define MY_FILTER_API_VERSION ${_filter_api}\")1" "${_filterdir}/filter_rwfrom.c" > "${_tmp}" && mv "${_tmp}" "${_filterdir}/filter_rwfrom.c"
	_ierr $? "patching filter source failed"
}

_configure() {
	local _filter_api="$1"
	case "${_filter_api}" in
		51-head)
			_bootstrap=1
			;;
		*)
			_bootstrap=0
			;;
	esac
	cd -- "${_ose}"
	if [ ${_bootstrap} -eq 1 ]; then
		env AUTOCONF_VERSION=2.69 sh bootstrap
		_ierr $? "bootstrap failed."
	fi
	_conf=""
	case `uname -s` in
		Linux)
			_conf="--prefix=/usr --sysconfdir=/etc/opensmtpd --localstatedir=/var --with-mantype=man --with-user-smtpd=smtpd --with-user-queue=smtpq --without-rpath"
			;;
		FreeBSD)
			_conf="--prefix=/usr/local --sysconfdir=/usr/local/etc/mail --localstatedir=/var --mandir=/usr/local/man"
			;;
	esac
	./configure ${_conf}
	_ierr $? "configure failed."
}

_build() {
	echo "- building OpenSMTPD-extras"
	cd -- "${_ose}"
	make
	_ierr $? "make failed."

	echo "- building filter-rwfrom"
	_filterdir=$(find ./ -type d -name filter-rwfrom)
	cd -- "${_filterdir}"
	make
	_ierr $? "make failed."

	echo "- success"
	cd -- "${_cdir}"
	ls -al -- "${_ose}/${_filterdir}/filter-rwfrom"
}


case "$1" in
	50)
		_filter_api="50"
		;;
	51)
		_filter_api="51"
		;;
	51-head)
		_filter_api="51-head"
		;;
	52)
		_filter_api="52"
		;;
	*)
		_usage
		;;
esac

_download "${_filter_api}"
_patch "${_filter_api}"
_configure "${_filter_api}"
_build "${_filter_api}"
