Name:    {{{ git_repo_name name="plm-fulcrum" }}}
Version: 0.1.0
Release: {{{ git_repo_version }}}%{?dist}
Summary: A fast & nimble SPV server for Palladium (PLM)

License:    GPLv3
URL:        https://github.com/palladium-coin/plm-fulcrum
VCS:        {{{ git_repo_vcs }}}

Source:     {{{ GIT_DIRTY=1 git_repo_pack }}}

BuildRequires: qt5-qtbase-devel
BuildRequires: bzip2-devel
BuildRequires: zlib-devel
BuildRequires: rocksdb-devel
BuildRequires: jemalloc-devel
BuildRequires: zeromq-devel
BuildRequires: miniupnpc-devel

BuildRequires: pandoc

Requires(pre): shadow-utils

BuildRequires: systemd systemd-rpm-macros
%{?systemd_requires}

%description
%{summary}.

%package admin
Summary: A fast & nimble SPV server for Palladium (PLM)

%description admin
%{summary}.

%prep
{{{ git_repo_setup_macro }}}

rm -rfv staticlibs/

%build
%qmake_qt5 "LIBS+=-lrocksdb -lz -lbz2"
%make_build
pandoc --standalone --from markdown-smart --to man doc/unix-man-page.md -o plm-fulcrum.1

%install
install -Dm 640 doc/plm-fulcrum-example-config.conf %{buildroot}/%{_sysconfdir}/plm-fulcrum.conf
install -Dm 755 plm-fulcrum %{buildroot}/%{_bindir}/plm-fulcrum
install -Dm 644 contrib/rpm/plm-fulcrum.service %{buildroot}/%{_unitdir}/plm-fulcrum.service
install -dm 750 %{buildroot}/%{_sharedstatedir}/plm-fulcrum
#doc
install -Dm 644 plm-fulcrum.1 %{buildroot}/%{_mandir}/man1/plm-fulcrum.1

#admin
install -Dm 755 plm-fulcrum-admin %{buildroot}/%{_bindir}/plm-fulcrum-admin

%pre
getent group plm-fulcrum >/dev/null || groupadd -r plm-fulcrum
getent passwd plm-fulcrum >/dev/null || \
    useradd -r -g plm-fulcrum -d %{_sharedstatedir}/plm-fulcrum -s /sbin/nologin \
    -c "plm-fulcrum SPV server for Palladium (PLM)" plm-fulcrum
exit 0

%post
%systemd_post plm-fulcrum.service

%preun
%systemd_preun plm-fulcrum.service

%postun
%systemd_postun_with_restart plm-fulcrum.service

%files
%doc README.md
%license LICENSE.txt NOTICE
%{_mandir}/man1/plm-fulcrum.1*
%config(noreplace) %attr(640, root, plm-fulcrum) %{_sysconfdir}/plm-fulcrum.conf
%{_bindir}/plm-fulcrum
%attr(700, plm-fulcrum, plm-fulcrum) %{_sharedstatedir}/plm-fulcrum
%{_unitdir}/plm-fulcrum.service

%files admin
%doc README.md
%license LICENSE.txt NOTICE
%{_bindir}/plm-fulcrum-admin

%changelog
{{{ git_repo_changelog }}}
