#!/bin/sh
# Latest GitHub Actions run per repo.
. scripts/common.sh
need GITHUB_REPOS

for repo in $GITHUB_REPOS; do
    if [ -n "$GITHUB_TOKEN" ]; then
        json=$(fetch "https://api.github.com/repos/$repo/actions/runs?per_page=1" \
            -H "Authorization: Bearer $GITHUB_TOKEN" 2>/dev/null)
    else
        json=$(fetch "https://api.github.com/repos/$repo/actions/runs?per_page=1" 2>/dev/null)
    fi
    status=$(echo "$json" | json_field status | head -n1)
    concl=$(echo "$json" | json_field conclusion | head -n1)
    branch=$(echo "$json" | json_field head_branch | head -n1)
    case "$status:$concl" in
        completed:success) mark="OK  " ;;
        completed:*)       mark="FAIL" ;;
        :*)                mark="??  " ;;
        *)                 mark="... " ;;
    esac
    echo "$mark ${repo#*/} ${branch:+($branch)}"
done
