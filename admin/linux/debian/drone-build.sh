#!/bin/bash

set -xe
shopt -s extglob

PPA=ppa:nextcloud-devs/client
PPA_BETA=ppa:nextcloud-devs/client-beta

OBS_PROJECT=home:ivaradi
OBS_PROJECT_BETA=home:ivaradi:beta
OBS_PACKAGE=nextcloud-client

pull_request=${DRONE_PULL_REQUEST:=master}

if test -z "${DRONE_WORKSPACE}"; then
    DRONE_WORKSPACE=`pwd`
fi

if test -z "${DRONE_DIR}"; then
    DRONE_DIR=`dirname ${DRONE_WORKSPACE}`
fi

set +x
if test "$DEBIAN_SECRET_KEY" -a "$DEBIAN_SECRET_IV"; then
    openssl aes-256-cbc -K $DEBIAN_SECRET_KEY -iv $DEBIAN_SECRET_IV -in admin/linux/debian/signing-key.txt.enc -d | gpg --import

    openssl aes-256-cbc -K $DEBIAN_SECRET_KEY -iv $DEBIAN_SECRET_IV -in admin/linux/debian/oscrc.enc -out ~/.oscrc -d

    touch ~/.has_ppa_keys
fi
set -x

cd "${DRONE_WORKSPACE}"
read basever kind <<<$(admin/linux/debian/scripts/git2changelog.py /tmp/tmpchangelog stable)

cd "${DRONE_DIR}"

echo "$kind" > kind

if test "$kind" = "beta"; then
    repo=nextcloud-devs/client-beta
else
    repo=nextcloud-devs/client
fi

origsourceopt=""

if ! wget http://ppa.launchpad.net/${repo}/ubuntu/pool/main/n/nextcloud-client/nextcloud-client_${basever}.orig.tar.bz2; then
    cp -a ${DRONE_WORKSPACE} nextcloud-client_${basever}
    tar cjf nextcloud-client_${basever}.orig.tar.bz2 --exclude .git nextcloud-client_${basever}
    origsourceopt="-sa"
fi

for distribution in xenial bionic cosmic disco stable; do
    rm -rf nextcloud-client_${basever}
    cp -a ${DRONE_WORKSPACE} nextcloud-client_${basever}

    cd nextcloud-client_${basever}

    cp -a admin/linux/debian/debian .
    if test -d admin/linux/debian/debian.${distribution}; then
        tar cf - -C admin/linux/debian/debian.${distribution} . | tar xf - -C debian
    fi

    admin/linux/debian/scripts/git2changelog.py /tmp/tmpchangelog ${distribution}
    cp /tmp/tmpchangelog debian/changelog
    if test -f admin/linux/debian/debian.${distribution}/changelog; then
        cat admin/linux/debian/debian.${distribution}/changelog >> debian/changelog
    else
        cat admin/linux/debian/debian/changelog >> debian/changelog
    fi

    for p in debian/post-patches/*.patch; do
        if test -f "${p}"; then
            echo "Applying ${p}"
            patch -p1 < "${p}"
        fi
    done

    fullver=`head -1 debian/changelog | sed "s:nextcloud-client (\([^)]*\)).*:\1:"`

    EDITOR=true dpkg-source --commit . local-changes

    dpkg-source --build .
    dpkg-genchanges -S ${origsourceopt} > "../nextcloud-client_${fullver}_source.changes"

    if test -f ~/.has_ppa_keys; then
        debsign -k7D14AA7B -S
    fi

    cd ..
done

if test "${pull_request}" = "master"; then
    kind=`cat kind`

    if test "$kind" = "beta"; then
        PPA=$PPA_BETA
        OBS_PROJECT=$OBS_PROJECT_BETA
    fi
    OBS_SUBDIR="${OBS_PROJECT}/${OBS_PACKAGE}"

    if test -f ~/.has_ppa_keys; then
        for changes in nextcloud-client_*~+([a-z])1_source.changes; do
            dput $PPA $changes > /dev/null
        done

        mkdir osc
        cd osc
        osc co ${OBS_PROJECT} ${OBS_PACKAGE}
        if test "$(ls ${OBS_SUBDIR})"; then
            osc delete ${OBS_SUBDIR}/*
        fi
        cp ../nextcloud-client*.orig.tar.* ${OBS_SUBDIR}/
        cp ../nextcloud-client_*[0-9.][0-9].dsc ${OBS_SUBDIR}/
        cp ../nextcloud-client_*[0-9.][0-9].debian.tar* ${OBS_SUBDIR}/
        cp ../nextcloud-client_*[0-9.][0-9]_source.changes ${OBS_SUBDIR}/
        osc add ${OBS_SUBDIR}/*

        cd ${OBS_SUBDIR}
        osc commit -m "Travis update"
    fi
fi
