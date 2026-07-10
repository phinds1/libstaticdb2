Name:        db2delta
Version:     %{version}
Release:     1%{?dist}
Summary:     Zero-dependency DB2 schema migration tool
License:     MIT
BuildArch:   x86_64
AutoReqProv: no
# musl static binaries don't carry a GNU build-id section
%global _missing_build_ids_terminate_build 0

%description
db2delta is a statically-compiled, zero-dependency DB2 schema migration
tool. No Java, no XML, no DB2 client libraries required. Ships as a
single static musl binary.

Apply versioned SQL changesets to a DB2 database, track applied changes
in a changelog table, and validate results — all from a single binary.

%install
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_sysconfdir}/bash_completion.d
mkdir -p %{buildroot}%{_mandir}/man1
install -m 0755 %{srcdir}/db2delta \
                %{buildroot}%{_bindir}/db2delta
install -m 0644 %{projdir}/completions/db2delta.bash \
                %{buildroot}%{_sysconfdir}/bash_completion.d/db2delta
install -m 0644 %{srcdir}/db2delta.1.gz \
                %{buildroot}%{_mandir}/man1/db2delta.1.gz

%files
%{_bindir}/db2delta
%{_sysconfdir}/bash_completion.d/db2delta
%{_mandir}/man1/db2delta.1.gz

%changelog
* Thu Jul 10 2025 Build System <build@localhost> - %{version}-1
- Automated package build
