#!/usr/bin/env node

const util = require('util');
const argv = require("minimist")(process.argv.slice(2));
const { demangle } = require("demangle");
const exec = util.promisify(require('child_process').exec);
const fs = require("fs");
const http = require("http");
const path = require("path");
const prettyBytes = require("pretty-bytes");
const readFile = util.promisify(fs.readFile);

try {
    if (!fs.lstatSync(argv.file).isFile())
        throw new Error(`${argv.file} is not a directory`);
} catch (err) {
    console.error(err.message);
    process.exit(1);
}

try {
    if (!fs.lstatSync(argv.exec).isFile())
        throw new Error(`${argv.exec} is not a file`);
} catch (err) {
    console.error(err.message);
    process.exit(1);
}

const addr2lineExec = argv.addr2line || "eu-addr2line";

const libs = [];

class Allocation
{
    constructor(size, func, libId, offset)
    {
        this.size = size;
        this["function"] = func;
        this.libId = libId;
        this.offset = offset;
    }
};

class Snapshot
{
    constructor(allocationIndex, bytes, allocationCount)
    {
        this.allocationIndex = allocationIndex;
        this.bytes = bytes;
        this.allocationCount = allocationCount;
    }

    toString()
    {
        return `index: ${this.allocationIndex} - ${prettyBytes(this.bytes)} - ${this.allocationCount} allocs`;
    }
}

let snapshots = [];
let index = [];
let measurements = 0;

class Options
{
    constructor()
    {
        this.file = argv.file;
        this.max = undefined;
        this.snapshotEvery = 10000;
        this.createLibs = false;
        this.silent = argv.silent;
        this.demangle = false;
        this.createIndex = false;
    }
};

async function processData(options)
{
    const data = await readFile(options.file, "utf8");
    let offset = 0;
    let last = 0;
    let count = 0;
    const current = {};
    let currentSize = 0;
    let bytesAllocated = 0;
    if (data.lastIndexOf("= Start\n", 0) === 0) {
        last = 8;
    }
    if (options.snapshotEvery) {
        snapshots = [];
    }
    const printEvery = options.max ? Math.max(1, Math.floor(options.max / 10)) : 10000;
    while (true) {
        const idx = data.indexOf("\n", last);
        if (idx === -1)
            break;

        const line = data.substring(last + 2, idx);

        let lib;
        let addr;
        let size; // undefined means free
        let ptr;
        let func;

        const parenEnd = line.indexOf(")[");
        let bracketStart;
        let bracketEnd;
        if (parenEnd !== -1) {
            const parenStart = line.lastIndexOf(":(", parenEnd);
            if (parenStart === -1) {
                throw new Error("Bad line " + line);
            }
            func = line.substring(parenStart + 2, parenEnd);
            lib = line.substring(0, parenStart);
            bracketStart = parenEnd + 1;
        } else {
            bracketStart = line.indexOf(":[") + 1;
            lib = line.substring(0, bracketStart - 1);
        }

        bracketEnd = line.indexOf("]", bracketStart);
        if (bracketEnd === -1) {
            throw new Error("Bad line 2 " + line);
        }

        addr = line.substring(bracketStart + 1, bracketEnd);

        const type = line[bracketEnd + 2];
        if (type === "+" || type === ">") {
            const lastSpace = line.lastIndexOf(" ");
            size = parseInt(line.substr(lastSpace + 1), 16);
            ptr = line.substring(bracketEnd + 4, lastSpace);
            let libId = libs.indexOf(lib);
            if (libId === -1) {
                // console.log("adding lib", libs, lib);
                libId = libs.size;
                libs.push(lib);
            }
            if (func && options.demangle) {
                func = demangle(func);
            }
            const alloc = new Allocation(size, func, libId, last);
            current[ptr] = alloc;
            ++currentSize;
            bytesAllocated += alloc.size;
        } else {
            ptr = line.substring(bracketEnd + 4);
            let c = current[ptr];
            if (c) {
                --currentSize;
                bytesAllocated -= c.size;
                // console.log("fuck3", bytesAllocated);
                delete current[ptr];
            } else {
                // console.log("Can't find free", ptr, current.keys());
            }
        }

        ++count;
        if (options.snapshotEvery && count % options.snapshotEvery === 0) {
            snapshots.push(new Snapshot(count, bytesAllocated, currentSize));
        }

        if (!options.silent && count % printEvery === 0) {
            if (options.max) {
                console.log(`Processed ${count}/${options.max} lines, ${((count / options.max) * 100.0).toFixed(1)}%`);
            } else {
                console.log(`Processed ${count} lines, ${prettyBytes(idx)}/${prettyBytes(data.length)} ${((idx / data.length) * 100.0).toFixed(1)}%`);
            }
        }
        if (count === options.max) {
            break;
        }
        last = idx + 1;
    }

    if (options.snapshotEvery !== undefined)
        measurements = count;

    // if (!options.silent) {
        // console.log("libs", libs);
        // console.log("snapshots", JSON.stringify(snapshots.map(x => x.toString()), undefined, 4));
    // }
    console.log("shit");
    return current;
}

async function handleRequest(req, res)
{
    console.log("got req", req.url);
    if (req.url === "/snapshots") {
        const snaps = snapshots.map(x => {
            return { allocationCount: x.allocationCount, bytes: x.bytes};
        });
        res.write(JSON.stringify({ snapshots: snaps, measurements }));
        res.end("\n");
        return;
    }

    if (req.url === "/libs") {
        res.write(JSON.stringify(libs));
        res.end("\n");
        return;
    }

    {
        const match = /\/data\/([0-9]*)/.exec(req.url);
        if (match) {
            const opts = new Options();
            opts.snapshotEvery = undefined;
            opts.demangle = true;
            opts.max = parseInt(match[1]) + 1;
            const allocs = await processData(opts);
            // console.log("it finished", allocs);
            res.write(JSON.stringify(allocs));
            res.end("\n");
            return;
        }
    }
}

async function createServer(port)
{
    return new Promise((resolve, reject) => {
        const server = http.createServer(handleRequest);
        server.listen(port);
        const onError = (err) => {
            server.off("listening", onListening);
            reject(err);
        };
        server.on("error", onError);
        const onListening = () => {
            server.off("error", onError);
            resolve();
        };
        server.on("listening", onListening);
    });
}

async function main()
{
    // console.log("got libs", libs);
    const options = new Options;
    options.max = 1000000;
    options.createLibs = true;
    await processData(options);
    await createServer(argv.port || 8888);
    console.log("listening on", argv.port || 8888);
}
main();
