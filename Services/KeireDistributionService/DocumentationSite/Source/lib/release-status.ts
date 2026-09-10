import projectConfiguration from "../../../../../Config/Project.conf?raw";
import publicationSource from "../../../Website/assets/preview-downloads.json?raw";
import { publicationState } from "./publication-state";

/** Source identity and publication state have separate authorities. Download URLs stay catalog-owned. */
export const releaseStatus = publicationState(projectConfiguration, JSON.parse(publicationSource).releaseStatus);
