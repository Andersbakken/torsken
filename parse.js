#!/usr/bin/env node

const argv = require("minimist")(process.argv.slice(2));
const fs = require("fs");
const path = require("path");
const util = require('util');
const exec = util.promisify(require('child_process').exec);
const readFile = util.promisify(fs.readFile);
const nexline = require("nexline");
const prettyBytes = require("pretty-bytes");
const demangle = require("demangle");

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
    constructor(size, libId, offset)
    {
        this.size = size;
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

async function processData(path)
{
    const data = await readFile(path, "utf8");
    let offset = 0;
    let last = 0;
    let count = 0;
    const current = new Map();
    const snapshots = [];
    let bytesAllocated = 0;
    if (data.lastIndexOf("= Start\n", 0) === 0) {
        last = 8;
    }
    while (true) {
        const idx = data.indexOf("\n", last);
        if (idx === -1)
            break;
        if (++count % 1000 === 0) {
            snapshots.push(new Snapshot(count, bytesAllocated, current.size));
        }

        const line = data.substring(last + 2, idx);
        const oldLast = last;
        last = idx + 1;

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
                libId = libs.size;
                libs.push(lib);
            }
            const alloc = new Allocation(size, libId, last);
            current.set(ptr, alloc);
            bytesAllocated += alloc.size;
        } else {
            ptr = line.substring(bracketEnd + 4);
            let c = current.get(ptr);
            if (c) {
                bytesAllocated -= c.size;
                // console.log("fuck3", bytesAllocated);
                current.delete(ptr);
            } else {
                // console.log("Can't find free", ptr, current.keys());
            }
        }
    }

    console.log("libs", libs);
    console.log("snapshots", JSON.stringify(snapshots.map(x => x.toString()), undefined, 4));
}

async function main()
{
    // console.log("got libs", libs);
    await processData(argv.file);
}
main();
