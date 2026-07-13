#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"; source "$ROOT/Scripts/Unix/common.sh"; load_project_config "$ROOT"
NAME="${1:-}"; DISPLAY="${2:-$NAME}"; REPOSITORY="${3:-}"
[[ "$NAME" =~ ^[A-Z][A-Za-z0-9]*$ ]] || { printf 'Name must be a PascalCase C++ identifier.\n' >&2; exit 1; }
[[ -z "$REPOSITORY" || "$REPOSITORY" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || { printf 'Repository must use owner/name format.\n' >&2; exit 1; }
new_core="${NAME}Core"; new_client="${NAME}Client"; new_tests="${NAME}Tests"; new_macro_prefix="$(identifier_to_macro_prefix "$NAME")"
escape_sed_replacement() { printf '%s' "$1" | sed 's/[\\\/&\#]/\\&/g'; }
display_replacement="$(escape_sed_replacement "$DISPLAY")"
repository_replacement="$(escape_sed_replacement "$REPOSITORY")"
for destination in "$new_core" "$new_client" "$new_tests"; do [[ ! -e "$ROOT/$destination" || "$destination" == "$CORE_DIRECTORY" || "$destination" == "$CLIENT_DIRECTORY" || "$destination" == "$TESTS_DIRECTORY" ]] || { printf 'Destination exists: %s\n' "$destination" >&2; exit 1; }; done
list="$(mktemp)"; backup="$(mktemp)"; completed=0
find "$ROOT" -type f | grep -Ev '/(\.git|Vendor|Tools|Build|Logs|\.vs)/' | sed "s#^$ROOT/##" > "$list"
tar -C "$ROOT" -cf "$backup" -T "$list"
rollback() {
    status=$?
    if [[ $completed -eq 0 ]]; then
        rm -rf "$ROOT/${new_core:?}" "$ROOT/${new_client:?}" "$ROOT/${new_tests:?}"
        tar -C "$ROOT" -xf "$backup"
    fi
    rm -f "$list" "$backup"
    exit "$status"
}
trap rollback EXIT
while IFS= read -r relative; do
    case "$relative" in Scripts/Windows/rename.ps1|Scripts/Unix/rename.sh|Scripts/Tests/*|Config/PackageConfig.cmake.in) continue ;; esac
    file="$ROOT/$relative"; [[ -f "$file" ]] || continue
    case "$file" in
      *.h|*.hpp|*.cpp|*.c)
        sed -e "s/$PROJECT_IDENTIFIER/$NAME/g" -e "s/$PROJECT_MACRO_PREFIX/$new_macro_prefix/g" -e "s/namespace $PROJECT_NAMESPACE/namespace $NAME/g" -e "s/$PROJECT_NAMESPACE::/$NAME::/g" -e "s#\"$PROJECT_NAMESPACE/#\"$NAME/#g" -e "s/\"$CORE_TARGET\"/\"$new_core\"/g" -e "s/\"$CLIENT_TARGET\"/\"$new_client\"/g" "$file" > "$file.tmp" && mv "$file.tmp" "$file"
        ;;
      *.md|*.yml|*.yaml|*.json|*.conf|*.lua|*.ps1|*.sh|*.txt|*.bat)
        if [[ -n "$REPOSITORY" ]]; then
          sed -e "s/$PROJECT_IDENTIFIER/$NAME/g" -e "s/$PROJECT_MACRO_PREFIX/$new_macro_prefix/g" -e "s/$PROJECT_DISPLAY_NAME/$display_replacement/g" -e "s#$REPOSITORY_SLUG#$repository_replacement#g" -e "s/$CORE_TARGET/$new_core/g" -e "s/$CLIENT_TARGET/$new_client/g" -e "s/$TESTS_TARGET/$new_tests/g" -e "s/$new_core\.h/Core.h/g" -e "s/$new_core::/$NAME::/g" -e "s/$NAME::$new_core/$NAME::Core/g" -e "s#$new_core/$new_core\.h#$NAME/Core.h#g" -e "s#$new_core/Log\.h#$NAME/Log.h#g" "$file" > "$file.tmp"
        else
          sed -e "s/$PROJECT_IDENTIFIER/$NAME/g" -e "s/$PROJECT_MACRO_PREFIX/$new_macro_prefix/g" -e "s/$PROJECT_DISPLAY_NAME/$display_replacement/g" -e "s/$CORE_TARGET/$new_core/g" -e "s/$CLIENT_TARGET/$new_client/g" -e "s/$TESTS_TARGET/$new_tests/g" -e "s/$new_core\.h/Core.h/g" -e "s/$new_core::/$NAME::/g" -e "s/$NAME::$new_core/$NAME::Core/g" -e "s#$new_core/$new_core\.h#$NAME/Core.h#g" -e "s#$new_core/Log\.h#$NAME/Log.h#g" "$file" > "$file.tmp"
        fi
        mv "$file.tmp" "$file"
        ;;
    esac
done < "$list"
printf 'PROJECT_IDENTIFIER=%s\nPROJECT_DISPLAY_NAME=%s\nPROJECT_VERSION=%s\nPROJECT_NAMESPACE=%s\nPROJECT_MACRO_PREFIX=%s\nCORE_TARGET=%s\nCORE_DIRECTORY=%s\nCLIENT_TARGET=%s\nCLIENT_DIRECTORY=%s\nTESTS_TARGET=%s\nTESTS_DIRECTORY=%s\nARTIFACT_PREFIX=%s\nREPOSITORY_SLUG=%s\n' "$NAME" "$DISPLAY" "$PROJECT_VERSION" "$NAME" "$new_macro_prefix" "$new_core" "$new_core" "$new_client" "$new_client" "$new_tests" "$new_tests" "$(printf '%s' "$NAME" | tr '[:upper:]' '[:lower:]')" "$REPOSITORY" > "$ROOT/Config/Project.conf"
[[ "$PROJECT_NAMESPACE" == "$NAME" ]] || mv "$ROOT/$CORE_DIRECTORY/Include/$PROJECT_NAMESPACE" "$ROOT/$CORE_DIRECTORY/Include/$NAME"
[[ "$CORE_DIRECTORY" == "$new_core" ]] || mv "$ROOT/$CORE_DIRECTORY" "$ROOT/$new_core"
[[ "$CLIENT_DIRECTORY" == "$new_client" ]] || mv "$ROOT/$CLIENT_DIRECTORY" "$ROOT/$new_client"
[[ "$TESTS_DIRECTORY" == "$new_tests" ]] || mv "$ROOT/$TESTS_DIRECTORY" "$ROOT/$new_tests"
completed=1
printf '==> Renamed template to %s. Review the unstaged changes before committing.\n' "$NAME"
