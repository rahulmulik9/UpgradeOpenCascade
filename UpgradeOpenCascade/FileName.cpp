bool RWGltf_CafWriter::writeBinData(const Handle(TDocStd_Document)& theDocument,
    const TDF_LabelSequence& theRootLabels,
    const TColStd_MapOfAsciiString* theLabelFilter,
    const Message_ProgressRange& theProgress)
{
#ifndef HAVE_DRACO
    if (myDracoParameters.DracoCompression)
    {
        Message::SendFail("Error: cannot use Draco compression, Draco library missing.");
        return false;
    }
#endif

    myBuffViewPos.Id = RWGltf_GltfAccessor::INVALID_ID;
    myBuffViewPos.ByteOffset = 0;
    myBuffViewPos.ByteLength = 0;
    myBuffViewPos.ByteStride = 12;
    myBuffViewPos.Target = RWGltf_GltfBufferViewTarget_ARRAY_BUFFER;

    myBuffViewNorm.Id = RWGltf_GltfAccessor::INVALID_ID;
    myBuffViewNorm.ByteOffset = 0;
    myBuffViewNorm.ByteLength = 0;
    myBuffViewNorm.ByteStride = 12;
    myBuffViewNorm.Target = RWGltf_GltfBufferViewTarget_ARRAY_BUFFER;

    myBuffViewTextCoord.Id = RWGltf_GltfAccessor::INVALID_ID;
    myBuffViewTextCoord.ByteOffset = 0;
    myBuffViewTextCoord.ByteLength = 0;
    myBuffViewTextCoord.ByteStride = 8;
    myBuffViewTextCoord.Target = RWGltf_GltfBufferViewTarget_ARRAY_BUFFER;

    myBuffViewInd.Id = RWGltf_GltfAccessor::INVALID_ID;
    myBuffViewInd.ByteOffset = 0;
    myBuffViewInd.ByteLength = 0;
    myBuffViewInd.Target = RWGltf_GltfBufferViewTarget_ELEMENT_ARRAY_BUFFER;

    myBuffViewsDraco.clear();

    myBinDataMap.Clear();
    myBinDataLen64 = 0;

    Message_ProgressScope aScope(theProgress, "Write binary data", myDracoParameters.DracoCompression ? 2 : 1);

    const Handle(OSD_FileSystem)& aFileSystem = OSD_FileSystem::DefaultFileSystem();
    std::shared_ptr<std::ostream> aBinFile = aFileSystem->OpenOStream(myBinFileNameFull, std::ios::out | std::ios::binary);
    if (aBinFile.get() == NULL
        || !aBinFile->good())
    {
        Message::SendFail(TCollection_AsciiString("File '") + myBinFileNameFull + "' can not be created");
        return false;
    }

    Message_ProgressScope aPSentryBin(aScope.Next(), "Binary data", 4);
    const RWGltf_GltfArrayType anArrTypes[4] =
    {
      RWGltf_GltfArrayType_Position,
      RWGltf_GltfArrayType_Normal,
      RWGltf_GltfArrayType_TCoord0,
      RWGltf_GltfArrayType_Indices
    };

    // dispatch faces
    NCollection_DataMap<XCAFPrs_Style, Handle(RWGltf_GltfFace), XCAFPrs_Style> aMergedFaces;
    for (XCAFPrs_DocumentExplorer aDocExplorer(theDocument, theRootLabels, XCAFPrs_DocumentExplorerFlags_OnlyLeafNodes);
        aDocExplorer.More() && aPSentryBin.More(); aDocExplorer.Next())
    {
        const XCAFPrs_DocumentNode& aDocNode = aDocExplorer.Current();
        if (theLabelFilter != NULL
            && !theLabelFilter->Contains(aDocNode.Id))
        {
            continue;
        }

        // transformation will be stored at scene nodes
        aMergedFaces.Clear(false);

        RWMesh_FaceIterator aFaceIter(aDocNode.RefLabel, TopLoc_Location(), true, aDocNode.Style);
        if (myToMergeFaces)
        {
            RWGltf_StyledShape aStyledShape(aFaceIter.ExploredShape(), aDocNode.Style);
            if (myBinDataMap.Contains(aStyledShape))
            {
                continue;
            }

            Handle(RWGltf_GltfFaceList) aGltfFaceList = new RWGltf_GltfFaceList();
            myBinDataMap.Add(aStyledShape, aGltfFaceList);
            for (; aFaceIter.More() && aPSentryBin.More(); aFaceIter.Next())
            {
                if (toSkipFaceMesh(aFaceIter))
                {
                    continue;
                }

                Handle(RWGltf_GltfFace) aGltfFace;
                if (!aMergedFaces.Find(aFaceIter.FaceStyle(), aGltfFace))
                {
                    aGltfFace = new RWGltf_GltfFace();
                    aGltfFaceList->Append(aGltfFace);
                    aGltfFace->Shape = aFaceIter.Face();
                    aGltfFace->Style = aFaceIter.FaceStyle();
                    aGltfFace->NbIndexedNodes = aFaceIter.NbNodes();
                    aMergedFaces.Bind(aFaceIter.FaceStyle(), aGltfFace);
                }
                else if (myToSplitIndices16
                    && aGltfFace->NbIndexedNodes < std::numeric_limits<uint16_t>::max()
                    && (aGltfFace->NbIndexedNodes + aFaceIter.NbNodes()) >= std::numeric_limits<uint16_t>::max())
                {
                    aMergedFaces.UnBind(aFaceIter.FaceStyle());
                    aGltfFace = new RWGltf_GltfFace();
                    aGltfFaceList->Append(aGltfFace);
                    aGltfFace->Shape = aFaceIter.Face();
                    aGltfFace->Style = aFaceIter.FaceStyle();
                    aGltfFace->NbIndexedNodes = aFaceIter.NbNodes();
                    aMergedFaces.Bind(aFaceIter.FaceStyle(), aGltfFace);
                }
                else
                {
                    if (aGltfFace->Shape.ShapeType() != TopAbs_COMPOUND)
                    {
                        TopoDS_Shape anOldShape = aGltfFace->Shape;
                        TopoDS_Compound aComp;
                        BRep_Builder().MakeCompound(aComp);
                        BRep_Builder().Add(aComp, anOldShape);
                        aGltfFace->Shape = aComp;
                    }
                    BRep_Builder().Add(aGltfFace->Shape, aFaceIter.Face());
                    aGltfFace->NbIndexedNodes += aFaceIter.NbNodes();
                }
            }
        }
        else
        {
            for (; aFaceIter.More() && aPSentryBin.More(); aFaceIter.Next())
            {
                RWGltf_StyledShape aStyledShape(aFaceIter.Face(), aFaceIter.FaceStyle());
                if (toSkipFaceMesh(aFaceIter)
                    || myBinDataMap.Contains(aStyledShape))
                {
                    continue;
                }

                Handle(RWGltf_GltfFaceList) aGltfFaceList = new RWGltf_GltfFaceList();
                Handle(RWGltf_GltfFace) aGltfFace = new RWGltf_GltfFace();
                aGltfFace->Shape = aFaceIter.Face();
                aGltfFace->Style = aFaceIter.FaceStyle();
                aGltfFaceList->Append(aGltfFace);
                myBinDataMap.Add(aStyledShape, aGltfFaceList);
            }
        }
    }

    std::vector<std::shared_ptr<RWGltf_CafWriter::Mesh>> aMeshes;
    Standard_Integer aNbAccessors = 0;
    NCollection_Map<Handle(RWGltf_GltfFaceList)> aWrittenFaces;
    NCollection_DataMap<TopoDS_Shape, Handle(RWGltf_GltfFace), TopTools_ShapeMapHasher> aWrittenPrimData;
    for (Standard_Integer aTypeIter = 0; aTypeIter < 4; ++aTypeIter)
    {
        const RWGltf_GltfArrayType anArrType = (RWGltf_GltfArrayType)anArrTypes[aTypeIter];
        RWGltf_GltfBufferView* aBuffView = NULL;
        switch (anArrType)
        {
        case RWGltf_GltfArrayType_Position: aBuffView = &myBuffViewPos;  break;
        case RWGltf_GltfArrayType_Normal:   aBuffView = &myBuffViewNorm; break;
        case RWGltf_GltfArrayType_TCoord0:  aBuffView = &myBuffViewTextCoord; break;
        case RWGltf_GltfArrayType_Indices:  aBuffView = &myBuffViewInd; break;
        default: break;
        }
        aBuffView->ByteOffset = aBinFile->tellp();
        aWrittenFaces.Clear(false);
        aWrittenPrimData.Clear(false);
        size_t aMeshIndex = 0;
        for (ShapeToGltfFaceMap::Iterator aBinDataIter(myBinDataMap); aBinDataIter.More() && aPSentryBin.More(); aBinDataIter.Next())
        {
            const Handle(RWGltf_GltfFaceList)& aGltfFaceList = aBinDataIter.Value();
            if (!aWrittenFaces.Add(aGltfFaceList)) // skip repeating faces
            {
                continue;
            }

            std::shared_ptr<RWGltf_CafWriter::Mesh> aMeshPtr;
            ++aMeshIndex;
#ifdef HAVE_DRACO
            if (myDracoParameters.DracoCompression)
            {
                if (aMeshIndex <= aMeshes.size())
                {
                    aMeshPtr = aMeshes.at(aMeshIndex - 1);
                }
                else
                {
                    aMeshes.push_back(std::make_shared<RWGltf_CafWriter::Mesh>(RWGltf_CafWriter::Mesh()));
                    aMeshPtr = aMeshes.back();
                }
            }
#endif

            for (RWGltf_GltfFaceList::Iterator aGltfFaceIter(*aGltfFaceList); aGltfFaceIter.More() && aPSentryBin.More(); aGltfFaceIter.Next())
            {
                const Handle(RWGltf_GltfFace)& aGltfFace = aGltfFaceIter.Value();

                Handle(RWGltf_GltfFace) anOldGltfFace;
                if (aWrittenPrimData.Find(aGltfFace->Shape, anOldGltfFace))
                {
                    switch (anArrType)
                    {
                    case RWGltf_GltfArrayType_Position:
                    {
                        aGltfFace->NodePos = anOldGltfFace->NodePos;
                        break;
                    }
                    case RWGltf_GltfArrayType_Normal:
                    {
                        aGltfFace->NodeNorm = anOldGltfFace->NodeNorm;
                        break;
                    }
                    case RWGltf_GltfArrayType_TCoord0:
                    {
                        aGltfFace->NodeUV = anOldGltfFace->NodeUV;
                        break;
                    }
                    case RWGltf_GltfArrayType_Indices:
                    {
                        aGltfFace->Indices = anOldGltfFace->Indices;
                        break;
                    }
                    default:
                    {
                        break;
                    }
                    }
                    continue;
                }
                aWrittenPrimData.Bind(aGltfFace->Shape, aGltfFace);

                for (RWMesh_FaceIterator aFaceIter(aGltfFace->Shape, aGltfFace->Style); aFaceIter.More() && aPSentryBin.More(); aFaceIter.Next())
                {
                    switch (anArrType)
                    {
                    case RWGltf_GltfArrayType_Position:
                    {
                        aGltfFace->NbIndexedNodes = 0; // reset to zero before RWGltf_GltfArrayType_Indices step
                        saveNodes(*aGltfFace, *aBinFile, aFaceIter, aNbAccessors, aMeshPtr);
                        break;
                    }
                    case RWGltf_GltfArrayType_Normal:
                    {
                        saveNormals(*aGltfFace, *aBinFile, aFaceIter, aNbAccessors, aMeshPtr);
                        break;
                    }
                    case RWGltf_GltfArrayType_TCoord0:
                    {
                        saveTextCoords(*aGltfFace, *aBinFile, aFaceIter, aNbAccessors, aMeshPtr);
                        break;
                    }
                    case RWGltf_GltfArrayType_Indices:
                    {
                        saveIndices(*aGltfFace, *aBinFile, aFaceIter, aNbAccessors, aMeshPtr);
                        break;
                    }
                    default:
                    {
                        break;
                    }
                    }

                    if (!aBinFile->good())
                    {
                        Message::SendFail(TCollection_AsciiString("File '") + myBinFileNameFull + "' cannot be written");
                        return false;
                    }
                }

                // add alignment by 4 bytes (might happen on RWGltf_GltfAccessorCompType_UInt16 indices)
                if (!myDracoParameters.DracoCompression)
                {
                    int64_t aContentLen64 = (int64_t)aBinFile->tellp();
                    while (aContentLen64 % 4 != 0)
                    {
                        aBinFile->write(" ", 1);
                        ++aContentLen64;
                    }
                }
            }
        }

        if (!myDracoParameters.DracoCompression)
        {
            aBuffView->ByteLength = (int64_t)aBinFile->tellp() - aBuffView->ByteOffset;
        }
        if (!aPSentryBin.More())
        {
            return false;
        }

        aPSentryBin.Next();
    }

    if (myDracoParameters.DracoCompression)
    {
#ifdef HAVE_DRACO
        OSD_Timer aDracoTimer;
        aDracoTimer.Start();
        draco::Encoder aDracoEncoder;
        aDracoEncoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, myDracoParameters.QuantizePositionBits);
        aDracoEncoder.SetAttributeQuantization(draco::GeometryAttribute::NORMAL, myDracoParameters.QuantizeNormalBits);
        aDracoEncoder.SetAttributeQuantization(draco::GeometryAttribute::TEX_COORD, myDracoParameters.QuantizeTexcoordBits);
        aDracoEncoder.SetAttributeQuantization(draco::GeometryAttribute::COLOR, myDracoParameters.QuantizeColorBits);
        aDracoEncoder.SetAttributeQuantization(draco::GeometryAttribute::GENERIC, myDracoParameters.QuantizeGenericBits);
        aDracoEncoder.SetSpeedOptions(myDracoParameters.CompressionLevel, myDracoParameters.CompressionLevel);

        std::vector<std::shared_ptr<draco::EncoderBuffer>> anEncoderBuffers(aMeshes.size());
        DracoEncodingFunctor aFunctor(aScope.Next(), aDracoEncoder, aMeshes, anEncoderBuffers);
        OSD_Parallel::For(0, int(aMeshes.size()), aFunctor, !myToParallel);

        for (size_t aBuffInd = 0; aBuffInd != anEncoderBuffers.size(); ++aBuffInd)
        {
            if (anEncoderBuffers.at(aBuffInd).get() == nullptr)
            {
                Message::SendFail(TCollection_AsciiString("Error: mesh not encoded in draco buffer."));
                return false;
            }
            RWGltf_GltfBufferView aBuffViewDraco;
            aBuffViewDraco.Id = (int)aBuffInd;
            aBuffViewDraco.ByteOffset = aBinFile->tellp();
            const draco::EncoderBuffer& anEncoderBuff = *anEncoderBuffers.at(aBuffInd);
            aBinFile->write(anEncoderBuff.data(), std::streamsize(anEncoderBuff.size()));
            if (!aBinFile->good())
            {
                Message::SendFail(TCollection_AsciiString("File '") + myBinFileNameFull + "' cannot be written");
                return false;
            }

            int64_t aLength = (int64_t)aBinFile->tellp();
            while (aLength % 4 != 0)
            {
                aBinFile->write(" ", 1);
                ++aLength;
            }

            aBuffViewDraco.ByteLength = aLength - aBuffViewDraco.ByteOffset;
            myBuffViewsDraco.push_back(aBuffViewDraco);
        }
        aDracoTimer.Stop();
        Message::SendInfo(TCollection_AsciiString("Draco compression time: ") + aDracoTimer.ElapsedTime() + " s");
#endif
    }

    if (myIsBinary
        && myToEmbedTexturesInGlb)
    {
        // save unique image textures
        for (XCAFPrs_DocumentExplorer aDocExplorer(theDocument, theRootLabels, XCAFPrs_DocumentExplorerFlags_OnlyLeafNodes);
            aDocExplorer.More() && aPSentryBin.More(); aDocExplorer.Next())
        {
            const XCAFPrs_DocumentNode& aDocNode = aDocExplorer.Current();
            if (theLabelFilter != NULL
                && !theLabelFilter->Contains(aDocNode.Id))
            {
                continue;
            }

            for (RWMesh_FaceIterator aFaceIter(aDocNode.RefLabel, TopLoc_Location(), true, aDocNode.Style);
                aFaceIter.More(); aFaceIter.Next())
            {
                if (toSkipFaceMesh(aFaceIter))
                {
                    continue;
                }

                myMaterialMap->AddGlbImages(*aBinFile, aFaceIter.FaceStyle());
            }
        }
    }

    int aBuffViewId = 0;
    if (myBuffViewPos.ByteLength > 0)
    {
        myBuffViewPos.Id = aBuffViewId++;
    }
    if (myBuffViewNorm.ByteLength > 0)
    {
        myBuffViewNorm.Id = aBuffViewId++;
    }
    if (myBuffViewTextCoord.ByteLength > 0)
    {
        myBuffViewTextCoord.Id = aBuffViewId++;
    }
    if (myBuffViewInd.ByteLength > 0)
    {
        myBuffViewInd.Id = aBuffViewId++;
    }
    // myMaterialMap->FlushGlbBufferViews() will put image bufferView's IDs at the end of list

    myBinDataLen64 = aBinFile->tellp();
    aBinFile->flush();
    if (!aBinFile->good())
    {
        Message::SendFail(TCollection_AsciiString("File '") + myBinFileNameFull + "' cannot be written");
        return false;
    }
    aBinFile.reset();
    return true;
}
