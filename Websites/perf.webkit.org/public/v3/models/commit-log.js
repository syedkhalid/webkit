'use strict';

class CommitLog extends DataModelObject {
    constructor(id, rawData)
    {
        console.assert(parseInt(id) == id);
        super(id);
        this._repository = rawData.repository;
        console.assert(this._repository instanceof Repository);
        this._rawData = rawData;
        this._remoteId = rawData.id;
        if (this._remoteId)
            this.ensureNamedStaticMap('remoteId')[this._remoteId] = this;
        this._ownedCommits = null;
        this._ownerCommit = null;
        this._ownedCommitByOwnedRepository = new Map;
    }

    updateSingleton(rawData)
    {
        super.updateSingleton(rawData);

        console.assert(+this._rawData['time'] == +rawData['time']);
        console.assert(this._rawData['revision'] == rawData['revision']);

        if (rawData.authorName)
            this._rawData.authorName = rawData.authorName;
        if (rawData.message)
            this._rawData.message = rawData.message;
        if (rawData.ownsCommits)
            this._rawData.ownsCommits = rawData.ownsCommits;
    }

    repository() { return this._repository; }
    time() { return new Date(this._rawData['time']); }
    author() { return this._rawData['authorName']; }
    revision() { return this._rawData['revision']; }
    message() { return this._rawData['message']; }
    url() { return this._repository.urlForRevision(this._rawData['revision']); }
    ownsCommits() { return this._rawData['ownsCommits']; }
    ownedCommits() { return this._ownedCommits; }
    ownerCommit() { return this._ownerCommit; }

    setOwnerCommits(ownerCommit) { this._ownerCommit = ownerCommit; }

    label()
    {
        const revision = this.revision();
        if (parseInt(revision) == revision) // e.g. r12345
            return 'r' + revision;
        if (revision.length == 40) // e.g. git hash
            return revision.substring(0, 8);
        return revision;
    }
    title() { return this._repository.name() + ' at ' + this.label(); }

    diff(previousCommit)
    {
        if (this == previousCommit)
            previousCommit = null;

        const repository = this._repository;
        if (!previousCommit)
            return {repository: repository, label: this.label(), url: this.url()};

        const to = this.revision();
        const from = previousCommit.revision();
        let label = null;
        if (parseInt(from) == from)// e.g. r12345.
            label = `r${from}-r${this.revision()}`;
        else if (to.length == 40) // e.g. git hash
            label = `${from.substring(0, 8)}..${to.substring(0, 8)}`;
        else
            label = `${from} - ${to}`;

        return {repository: repository, label: label, url: repository.urlForRevisionRange(from, to)};
    }

    static fetchLatestCommitForPlatform(repository, platform)
    {
        console.assert(repository instanceof Repository);
        console.assert(platform instanceof Platform);
        return this.cachedFetch(`/api/commits/${repository.id()}/latest`, {platform: platform.id()}).then((data) => {
            const commits = data['commits'];
            if (!commits || !commits.length)
                return null;
            const rawData = commits[0];
            rawData.repository = repository;
            return CommitLog.ensureSingleton(rawData.id, rawData);
        });
    }

    ownedCommitForOwnedRepository(ownedRepository) { return this._ownedCommitByOwnedRepository.get(ownedRepository); }

    fetchOwnedCommits()
    {
        if (!this.repository().ownedRepositories())
            return Promise.reject();

        if (!this.ownsCommits())
            return Promise.reject();

        if (this._ownedCommits)
            return Promise.resolve(this._ownedCommits);

        return CommitLog.cachedFetch(`../api/commits/${this.repository().id()}/owned-commits?owner-revision=${escape(this.revision())}`).then((data) => {
            this._ownedCommits = CommitLog._constructFromRawData(data);
            this._ownedCommits.forEach((ownedCommit) => {
                ownedCommit.setOwnerCommits(this);
                this._ownedCommitByOwnedRepository.set(ownedCommit.repository(), ownedCommit);
            });
            return this._ownedCommits;
        });
    }

    _buildOwnedCommitMap()
    {
        const ownedCommitMap = new Map;
        for (const commit of this._ownedCommits)
            ownedCommitMap.set(commit.repository(), commit);
        return ownedCommitMap;
    }

    static ownedCommitDifferenceForOwnerCommits(...commits)
    {
        console.assert(commits.length >= 2);

        const ownedCommitRepositories = new Set;
        const ownedCommitMapList = commits.map((commit) => {
            console.assert(commit);
            console.assert(commit._ownedCommits);
            const ownedCommitMap = commit._buildOwnedCommitMap();
            for (const repository of ownedCommitMap.keys())
                ownedCommitRepositories.add(repository);
            return ownedCommitMap;
        });

        const difference = new Map;
        ownedCommitRepositories.forEach((ownedCommitRepository) => {
            const ownedCommits = ownedCommitMapList.map((ownedCommitMap) => ownedCommitMap.get(ownedCommitRepository));
            const uniqueOwnedCommits = new Set(ownedCommits);
            if (uniqueOwnedCommits.size > 1)
                difference.set(ownedCommitRepository, ownedCommits);
        });
        return difference;
    }

    static fetchBetweenRevisions(repository, precedingRevision, lastRevision)
    {
        // FIXME: The cache should be smarter about fetching a range within an already fetched range, etc...
        // FIXME: We should evict some entires from the cache in cachedFetch.
        return this.cachedFetch(`/api/commits/${repository.id()}/`, {precedingRevision, lastRevision})
            .then((data) => this._constructFromRawData(data));
    }

    static fetchForSingleRevision(repository, revision)
    {
        return this.cachedFetch(`/api/commits/${repository.id()}/${revision}`).then((data) => {
            return this._constructFromRawData(data);
        });
    }

    static _constructFromRawData(data)
    {
        return data['commits'].map((rawData) => {
            rawData.repository = Repository.findById(rawData.repository);
            return CommitLog.ensureSingleton(rawData.id, rawData);
        });
    }
}

if (typeof module != 'undefined')
    module.exports.CommitLog = CommitLog;
