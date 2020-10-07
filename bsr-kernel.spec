# Conditionals
%bcond_with modsign

# BSR-659 disable debug pakage build
%define debug_package %{nil}
%define __strip /bin/true

Name: bsr-kernel
Summary: Kernel driver for BSR
Version: 1.6
Release: A4%{?dist}

# always require a suitable userland
# Requires: 

%global tarball_version %(echo "%{version}-%{?release}" | sed -e "s,%{?dist}$,,")
Source: bsr-%{tarball_version}.tar.gz
License: GPLv2+
Group: System Environment/Kernel
# URL: 
BuildRoot: %(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)
BuildRequires: redhat-rpm-config
%if %{defined kernel_module_package_buildreqs}
BuildRequires: %kernel_module_package_buildreqs
%endif

%description
This module is the kernel-dependent driver for BSR.  This is split out so
that multiple kernel driver versions can be installed, one for each
installed kernel.

%prep
%setup -q -n bsr-%{tarball_version}

# Concept stolen from sles kernel-module-subpackage:
# include the kernel version in the package version,
# so we can have more than one kmod-bsr.
# Needed, because even though kABI is still "compatible" in RHEL 6.0 to 6.1,
# the actual functionality differs very much: 6.1 does no longer do BARRIERS,
# but wants FLUSH/FUA instead.
# For convenience, we want both 6.0 and 6.1 in the same repository,
# and have yum/rpm figure out via dependencies, which kmod version should be installed.
# This is a dirty hack, non generic, and should probably be enclosed in some "if-on-rhel6".
%define _this_kmp_version %{version}_%(echo %kernel_version | sed -r 'y/-/_/; s/\.el.\.(x86_64|i.86)$//;')
%kernel_module_package -v %_this_kmp_version -n bsr -f filelist-redhat


%build
rm -rf obj
mkdir obj
ln -s ../bsr-headers obj/
ln -s ../bsr-platform obj/

for flavor in %flavors_to_build; do
    cp -r bsr obj/$flavor
    #make -C %{kernel_source $flavor} M=$PWD/obj/$flavor
    make -C obj/$flavor %{_smp_mflags} all KDIR=%{kernel_source $flavor}
    # BSR-659 module sign for secure boot support
    %if %{with modsign}
    ln -s ../pki obj/
    make -C obj/$flavor modsign
    %endif
done

%install
export INSTALL_MOD_PATH=$RPM_BUILD_ROOT

%if %{defined kernel_module_package_moddir}
export INSTALL_MOD_DIR=%{kernel_module_package_moddir bsr}
%else
export INSTALL_MOD_DIR=extra/bsr
%endif

# Very likely kernel_module_package_moddir did ignore the parameter,
# so we just append it here. The weak-modules magic expects that location.
[ $INSTALL_MOD_DIR = extra ] && INSTALL_MOD_DIR=extra/bsr

for flavor in %flavors_to_build ; do
    make -C %{kernel_source $flavor} modules_install \
	M=$PWD/obj/$flavor
    kernelrelease=$(cat %{kernel_source $flavor}/include/config/kernel.release || make -s -C %{kernel_source $flavor} kernelrelease)
    find $INSTALL_MOD_PATH/lib/modules -iname 'modules.*' -exec rm {} \;
    mv obj/$flavor/.kernel.config.gz obj/k-config-$kernelrelease.gz
    mv obj/$flavor/Module.symvers ../../RPMS/Module.symvers.$kernelrelease.$flavor.%{_arch}
done

# BSR-659 install public key for secure boot support
%if %{with modsign}
mkdir -p $RPM_BUILD_ROOT/etc/pki/mantech
install -m 0644 pki/bsr_signing_key_pub.der $RPM_BUILD_ROOT/etc/pki/mantech
%endif

mkdir -p $RPM_BUILD_ROOT/var/log/bsr

mkdir -p $RPM_BUILD_ROOT/etc/depmod.d
echo "override bsr * weak-updates" \
    > $RPM_BUILD_ROOT/etc/depmod.d/bsr.conf

%clean
rm -rf %{buildroot}

%changelog
* Mon Aug 31 2020 Man Technology Inc. <bsr@mantech.co.kr> - 1.6-A4
- New upstream release.

* Tue Aug 25 2020 Man Technology Inc. <bsr@mantech.co.kr> - 1.6-A3
- New upstream release.

* Mon Jul 13 2020 Man Technology Inc. <bsr@mantech.co.kr> - 1.6-A2
- New upstream release.

* Mon Jul 6 2020 Man Technology Inc. <bsr@mantech.co.kr> - 1.6-A1
- New upstream release.

* Wed Jun 24 2020 Man Technology Inc. <bsr@mantech.co.kr> - 1.6.0-PREALPHA5
- New upstream release.

* Wed May 06 2020 Man Technology Inc. <bsr@mantech.co.kr> - 1.6.0-PREALPHA4
- New upstream release.

* Mon Jan 06 2020 Man Technology Inc. <bsr@mantech.co.kr> - 1.6.0-PREALPHA3
- New upstream release.