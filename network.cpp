#include "network.hpp"

bool network::load_from_xml(const char *filename)
{
    xmlTextReaderPtr reader = xmlReaderForFile(filename, NULL, XML_PARSE_XINCLUDE);
    if(!reader)
    {
        fprintf(stderr, "Couldn't open %s for reading\n", filename);
        return false;
    }

    int ret = xmlTextReaderRead(reader);
    while (ret == 1 && !is_opening_element(reader, "network"))
        ret = xmlTextReaderRead(reader);

    if(ret != 1)
    {
        if(ret == -1)
            fprintf(stderr, "Parsing error in %s\n", filename);
        else if(ret == 0)
            fprintf(stderr, "Couldn't find network element in %s\n", filename);

        xmlFreeTextReader(reader);
        return false;
    }

    xmlNodePtr np = xmlTextReaderCurrentNode(reader);
    if(!np)
    {
        fprintf(stderr, "Bad node pointer!\n");
        xmlFreeTextReader(reader);
        return false;
    }

    if(xmlXIncludeProcessTree(np) == -1)
    {
        fprintf(stderr, "Couldn't include!\n");
        xmlFreeTextReader(reader);
        return false;
    }

    bool status = xml_read(reader);

    xmlFreeTextReader(reader);
    xmlCleanupParser();

    return status;
}

bool network::xml_read(xmlTextReaderPtr reader)
{
    float version = 0.0f;

    boost::fusion::vector<list_matcher<float>,
        list_matcher<char*> > vl(lm("version", &version),
                                 lm("name", &name));

    if(!read_attributes(vl, reader))
       return false;

    if(version != 1.0f)
    {
        fprintf(stderr, "Network version is %f, expected 1.0f!\n", version);
        return false;
    }

    std::map<char*, int, ltstr> road_refs;
    std::map<char*, int, ltstr> lane_refs;
    std::map<char*, int, ltstr> intersection_refs;

    bool have_roads = false;
    bool have_lanes = false;
    bool have_intersections = false;

    int ret;
    do
    {
        ret = xmlTextReaderRead(reader);
        if(ret != 1)
            return false;

        if(xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT)
        {
            const xmlChar *name = xmlTextReaderConstName(reader);
            if(!name)
                return false;

            if(xmlStrEqual(name, BAD_CAST "roads") && !have_roads)
                have_roads = read_sequence(roads, road_refs, reader, "road", "roads");
            else if(xmlStrEqual(name, BAD_CAST "lanes") && !have_lanes)
                have_lanes = read_sequence(lanes, lane_refs, reader, "lane", "lanes");
            else if(xmlStrEqual(name, BAD_CAST "intersections") && !have_intersections)
                have_intersections = read_sequence(intersections, intersection_refs, reader, "intersection", "intersections");
            else
                return false;
        }
    }
    while(ret == 1 && !is_closing_element(reader, "network"));

    printf("Read %zu roads\n", roads.size());
    printf("Read %zu lanes\n", lanes.size());
    printf("Read %zu intersections\n", intersections.size());

    std::vector<lane>::iterator lane_itr = lanes.begin();
    for(; lane_itr != lanes.end(); ++lane_itr)
    {
        // convert end points
        if(lane_itr->start.end_type == lane_end::INTERSECTION)
            if(!lane_itr->start.inters.retrieve_ptr(intersections, intersection_refs))
                return false;

        if(lane_itr->end.end_type == lane_end::INTERSECTION)
            if(!lane_itr->end.inters.retrieve_ptr(intersections, intersection_refs))
                return false;

        // convert road_memberships
        road_intervals & ri = lane_itr->road_memberships;
        if(!ri.base_data.parent_road.retrieve_ptr(roads, road_refs))
            return false;

        for(int i = 0; i < static_cast<int>(ri.entries.size()); ++i)
            if(!ri.entries[i].data.parent_road.retrieve_ptr(roads, road_refs))
                return false;
    }

    std::vector<intersection>::iterator intersection_itr = intersections.begin();
    for(; intersection_itr != intersections.end(); ++intersection_itr)
    {
        for(int i = 0; i < static_cast<int>(intersection_itr->incoming.size()); ++i)
            if(!intersection_itr->incoming[i].retrieve_ptr(lanes, lane_refs))
                return false;

        for(int i = 0; i < static_cast<int>(intersection_itr->outgoing.size()); ++i)
            if(!intersection_itr->outgoing[i].retrieve_ptr(lanes, lane_refs))
                return false;
    }

    std::map<char*, int, ltstr>::iterator current = road_refs.begin();
    for(; current != road_refs.end(); ++current)
        free(current->first);

    current = lane_refs.begin();
    for(; current != lane_refs.end(); ++current)
        free(current->first);

    current = intersection_refs.begin();
    for(; current != intersection_refs.end(); ++current)
        free(current->first);

    return ret == 1 && have_roads && have_lanes && have_intersections;
}

network::~network()
{
    free(name);
}